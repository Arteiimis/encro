#include "video/video_batch_execution.h"

#include "video/encode_probe.h"
#include "video/video_encode_runner.h"
#include "video/video_workflow_utils.h"

#include "core/display_text.h"
#include "core/encoding_state.h"
#include "core/job_state.h"
#include "core/media_item.h"
#include "core/task_executor.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "utils/utils.h"

#include "logging/log_tags.h"
#include "logging/logging.h"
#include "logging/setup.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <thread>
#include <unordered_map>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::VIDEO_BATCH);

namespace fs = std::filesystem;
using enum terminal::MessageKind;
using videobatch::detail::EncodingExecutionContext;
using videobatch::detail::EncodingProgressState;
using videoworkflow::maybeJobState;
using videoworkflow::noteStopRequest;
using videoworkflow::withJobState;

namespace videobatch::detail {

auto persistedElapsedMs(
  jobstate::Store& store,
  std::optional<std::string> const& actionId
) -> std::chrono::milliseconds {
  if (!actionId.has_value()) { return std::chrono::milliseconds{0}; }
  auto const record = store.findTask(actionId.value());
  if (!record.has_value() || !record->encodedMs.has_value()) {
    return std::chrono::milliseconds{0};
  }
  return std::chrono::milliseconds{record->encodedMs.value()};
}

}  // namespace videobatch::detail

namespace {

// One file's encode bookkeeping, shared by the parallel stage and the verbose
// sequential stage: the job-state running/settled transitions, the item's
// final state and the progress-file cleanup. Bars and log lines stay with the
// caller, which is all the two modes differ in.
//
// Returns the failure text to record on the item, or nullopt on success. The
// text is empty when the encoder recorded no reason of its own.
auto encodeOneItem(
  appctx::AppContext& ctx,
  appctx::EncodingState& item,
  function_ref statusUpdater,
  std::size_t workerCount = 1
) -> std::optional<std::string> {
  {
    auto lock = std::scoped_lock{item.mtx};
    if (auto* store = maybeJobState(ctx); item.actionId.has_value()) {
      store->markRunning(item.actionId.value());
    }
  }

  auto const success =
    encodeVideo(ctx, item, ctx.config.outputFormat, statusUpdater, workerCount);

  auto progressFileToRemove = std::optional<fs::path>{};
  auto lastStatus = std::optional<std::string>{};
  auto failureText = std::string{};
  {
    auto lock = std::scoped_lock{item.mtx};
    if (
      success
      && item.plannedOutputFile.has_value()
      && fs::exists(item.plannedOutputFile.value())
    ) {
      item.outputFile = item.plannedOutputFile;
    }
    item.finished = true;
    item.success = success;
    item.endTime = std::chrono::steady_clock::now();
    item.lastProgressAtomic.store(100.0f, std::memory_order_release);
    progressFileToRemove = item.progressFilePath;
    lastStatus = item.lastStatus;
    failureText = item.lastError.value_or(item.lastStatus.value_or(""));
  }

  if (progressFileToRemove.has_value()) {
    auto ec = std::error_code{};
    fs::remove(progressFileToRemove.value(), ec);
  }

  if (auto* store = maybeJobState(ctx); item.actionId.has_value()) {
    if (success) {
      if (lastStatus.has_value()) {
        store->markSucceeded(item.actionId.value(), lastStatus.value());
      } else {
        store->markSucceeded(item.actionId.value());
      }
    } else {
      store->markFailed(
        item.actionId.value(),
        failureText.empty() ? "encoding failed" : failureText
      );
    }
  }

  if (success) { return std::nullopt; }
  return failureText;
}

auto makeSlotLabel(fs::path const& vidPath) -> std::string {
  return displaytext::pathToUtf8String(vidPath.filename());
}

void reportEncodingStatus(
  EncodingExecutionContext& executionCtx,
  appctx::EncodingState& vidState,
  std::string const& fileLabel,
  std::string const& status
) {
  executionCtx.barEncodingStatus(vidState, fileLabel, status);
  auto actionId = std::optional<std::string>{};
  auto lock = std::scoped_lock{vidState.mtx};
  vidState.lastStatus = status;
  actionId = vidState.actionId;
  if (auto* store = maybeJobState(executionCtx.app); actionId.has_value()) {
    store->markProgress(actionId.value(), std::nullopt, std::nullopt, status);
  }
}

// The item was built at planning; the run only stamps the slot it took and
// when it started.
void startEncodingState(
  appctx::EncodingState& item,
  std::optional<std::size_t> barIndex
) {
  item.barIndex = barIndex;
  item.startTime = std::chrono::steady_clock::now();
}

// Records the message of an exception that escaped runEncodingTask (which
// would otherwise leave no specific failure reason and a stale active slot)
// onto the slot's encoding state, then clears the slot.
void recordTaskException(
  EncodingExecutionContext& executionCtx,
  std::size_t slot,
  std::string_view message
) {
  auto const state = executionCtx.activeState(slot);
  if (state) {
    auto lock = std::scoped_lock{state->mtx};
    state->lastError = std::string{message};
  }
  executionCtx.clearActive(slot);
}

// The post-encode fields the parallel path logs.
struct EncodingOutcome {
  std::optional<fs::path> outputFile_;
  int64_t elapsedMs_ = 0;
};

auto collectOutcome(appctx::EncodingState& vidState) -> EncodingOutcome {
  auto outcome = EncodingOutcome{};
  auto lock = std::scoped_lock{vidState.mtx};
  outcome.outputFile_ = vidState.outputFile;
  if (vidState.startTime.has_value() && vidState.endTime.has_value()) {
    using namespace std::chrono;
    auto const elapsed = vidState.endTime.value() - vidState.startTime.value();
    outcome.elapsedMs_ = duration_cast<milliseconds>(elapsed).count();
  }
  return outcome;
}

auto runEncodingTask(
  EncodingExecutionContext& executionCtx,
  appctx::EncodingStatePtr const& item,
  std::size_t taskIndex,
  std::size_t slot
) -> eh::Result<void> {
  if (stopsignal::isStopRequested()) {
    noteStopRequest(executionCtx.app);
    return eh::makeError("Encoding canceled by user.");
  }

  LOG_DEBUG(
    "[slot:{} task:{}/{}] start encoding: {}",
    slot + 1,
    taskIndex + 1,
    executionCtx.pendingTotal(),
    item->inputPath.string()
  );
  auto const barIndex = executionCtx.barIndexOpt(slot);
  startEncodingState(*item, barIndex);
  executionCtx.setActive(slot, item);

  auto const fileLabel = makeSlotLabel(item->inputPath);
  auto elapsedBase = std::chrono::milliseconds{0};
  if (auto* store = maybeJobState(executionCtx.app); store != nullptr) {
    elapsedBase = videobatch::detail::persistedElapsedMs(*store, item->actionId);
  }
  executionCtx.barEncodingStart(*item, fileLabel, elapsedBase);

  auto const failureText = encodeOneItem(
    executionCtx.app,
    *item,
    [&](std::string const& status) {
      reportEncodingStatus(executionCtx, *item, fileLabel, status);
    },
    executionCtx.counters().workers
  );

  auto const outcome = collectOutcome(*item);
  if (failureText.has_value()) {
    LOG_WARN(
      "[slot:{} task:{}/{}] encoded failed: {} ({} ms)",
      slot + 1,
      taskIndex + 1,
      executionCtx.pendingTotal(),
      item->inputPath.string(),
      outcome.elapsedMs_
    );
  } else {
    LOG_INFO(
      "[slot:{} task:{}/{}] encoded success: {} -> {} ({} ms)",
      slot + 1,
      taskIndex + 1,
      executionCtx.pendingTotal(),
      item->inputPath.string(),
      outcome.outputFile_.has_value() ? outcome.outputFile_->string() : "<unknown>",
      outcome.elapsedMs_
    );
  }

  executionCtx.barDone(barIndex, !failureText.has_value(), fileLabel);
  executionCtx.clearActive(slot);

  // A failure the encoder left unexplained still names itself, so the summary
  // prints a line for every failed file.
  if (failureText.has_value()) {
    return eh::makeError(
      "{}",
      failureText->empty() ? "encoding failed" : failureText.value()
    );
  }
  return {};
}

// One verbose file: the no-progress log lines around the shared bookkeeping.
// The failure text the encoder recorded is returned as it is, so a failure
// with no reason of its own prints no summary line (as it always did here).
auto runVerboseEncodingItem(appctx::AppContext& ctx, appctx::EncodingState& item)
  -> eh::Result<void> {
  LOG_DEBUG("Start encoding (no-progress): {}", item.inputPath.string());
  auto const failureText = encodeOneItem(ctx, item, {});
  if (failureText.has_value()) {
    LOG_WARN("Encoded failed (no-progress): {}", item.inputPath.string());
    return eh::makeError("{}", failureText.value());
  }
  LOG_INFO("Encoded success (no-progress): {}", item.inputPath.string());
  return {};
}

void runEncodingWithoutProgress(
  appctx::AppContext& ctx,
  std::span<appctx::EncodingStatePtr const> items
) {
  LOG_INFO(
    "Running encoding without progress bars (verbose output mode), total={}.",
    items.size()
  );

  // The verbose path is the parallel stage with one worker and no bar of its
  // own: the runner owns the per-file outcome write-back, and the job-state
  // transitions live in encodeOneItem.
  mediaitem::runStage(
    mediaitem::StageSpec{
      .progress = nullptr,
      .maxConcurrency = 1,
    },
    items,
    [](appctx::EncodingState const&) { return false; },
    [&ctx](appctx::EncodingState& item, taskexec::TaskContext&) {
      return runVerboseEncodingItem(ctx, item);
    }
  );
}

}  // namespace

namespace {

enum class ProbeStageStatus {
  Proceed,
  Aborted,
  Failed,
  DryRun
};

// Probing stage of runEncodingTasks (MP4 only; --crf bypasses it entirely):
// picks a per-file CQ meeting the quality floor and prints the plan before
// the confirmation prompt. Writes each item's chosen CQ and fills
// attentionWarnings on success; encodableItems receives the items that
// survive the probe (plans whose estimated size exceeds the source are
// dropped).
auto runProbeStage(
  appctx::AppContext& ctx,
  std::span<appctx::EncodingStatePtr const> items,
  std::vector<appctx::EncodingStatePtr>& encodableItems,
  std::vector<std::string>& attentionWarnings
) -> ProbeStageStatus {
  auto const shouldProbe =
    ctx.config.outputFormat == "mp4" && !ctx.config.crf.has_value();
  if (!shouldProbe) {
    encodableItems.assign(items.begin(), items.end());
    return ProbeStageStatus::Proceed;
  }

  // The probe phase plans by path and keeps its own plan list, so the paths go
  // in and the decisions come back onto the items.
  auto vids = std::vector<fs::path>{};
  vids.reserve(items.size());
  for (auto const& item: items) { vids.push_back(item->inputPath); }

  auto const probeStartedAt = std::chrono::steady_clock::now();
  auto probeRes = encodeprobe::runProbePhase(ctx, vids);
  auto const probeElapsed = displaytext::elapsedSince(probeStartedAt);
  if (stopsignal::isStopRequested()) {
    noteStopRequest(ctx);
    return ProbeStageStatus::Aborted;
  }
  if (!probeRes) {
    LOG_ERROR("{}", probeRes.error());
    terminal::messageln(Error, "{}", probeRes.error());
    return ProbeStageStatus::Failed;
  }
  // Plans marked skipEncode (estimated output > source) never reach the
  // encode stage; the printed plan flags them as skipped.
  encodableItems.clear();
  for (auto const& item: items) {
    auto const it = probeRes->plans.find(item->inputPath);
    if (it == probeRes->plans.end()) {
      encodableItems.push_back(item);
      continue;
    }
    item->chosenCq = it->second.chosenCq;
    if (it->second.skipEncode) { continue; }
    encodableItems.push_back(item);
  }
  attentionWarnings = std::move(probeRes->attentionWarnings);

  auto plans = std::vector<encodeprobe::ProbePlan>{};
  plans.reserve(probeRes->plans.size());
  for (auto const& item: items) {
    if (
      auto const it = probeRes->plans.find(item->inputPath); it != probeRes->plans.end()
    ) {
      plans.push_back(it->second);
    }
  }
  encodeprobe::printProbePlan(plans, ctx.config.minVmaf, probeElapsed);

  if (ctx.config.dryRun) {
    LOG_INFO("Dry run: probe plan printed; exiting without encoding.");
    return ProbeStageStatus::DryRun;
  }
  return ProbeStageStatus::Proceed;
}

}  // namespace

// Restores the forensic app context when the batch (and its monitor thread)
// goes out of scope.
struct ForensicContextGuard {
  ~ForensicContextGuard() { logging::setForensicAppContext(nullptr); }
};

// EncodingProgressState holds atomics and is neither copyable nor movable,
// so the state lives on the heap for the duration of the batch.
struct PreparedEncodingExecution {
  std::unique_ptr<EncodingProgressState> progressState;
  std::unique_ptr<EncodingExecutionContext> ctx;
  ForensicContextGuard forensicGuard;
  std::jthread monitorThread;
  std::size_t maxConcurrentJobs;
};

void runVerboseEncoding(
  appctx::AppContext& ctx,
  std::span<appctx::EncodingStatePtr const> items
) {
  // One notice covers both echo levels; suppressed under --quiet, where bars
  // are already off (verbose-levels D5/D6).
  if (!terminal::quiet()) {
    terminal::messageln(Warning, "Echo enabled: progress bars disabled.");
  }
  runEncodingWithoutProgress(ctx, items);
}

auto prepareEncodingExecution(
  appctx::AppContext& ctx,
  std::span<appctx::EncodingStatePtr const> items,
  std::size_t overallTotalCount,
  std::size_t initialCompletedCount
) -> PreparedEncodingExecution {
  constexpr auto kMaxConcurrentJobs = std::size_t{10};
  auto const maxConcurrentJobs =
    std::max<std::size_t>(1, ctx.config.maxParallelJobs.value_or(kMaxConcurrentJobs));
  auto const workerCount = taskexec::resolveWorkerCount(items.size(), maxConcurrentJobs);
  auto const compact = !ctx.config.fullProgress;
  auto progressState = std::make_unique<
    EncodingProgressState
  >(items.size(), overallTotalCount, initialCompletedCount, workerCount, compact);

  LOG_INFO(
    "Scheduling encoding workers: workers={} pending={} overall={} "
    "completed-before-start={}",
    workerCount,
    items.size(),
    overallTotalCount,
    initialCompletedCount
  );

  auto executionCtx = std::make_unique<EncodingExecutionContext>(ctx, *progressState);
  executionCtx->updateOverall();

  logging::setForensicAppContext(&ctx);

  auto monitorThread = videobatch::detail::startEncodingMonitor(*executionCtx);
  return PreparedEncodingExecution{
    .progressState = std::move(progressState),
    .ctx = std::move(executionCtx),
    .forensicGuard = {},
    .monitorThread = std::move(monitorThread),
    .maxConcurrentJobs = maxConcurrentJobs,
  };
}

void logBatchStart(
  appctx::AppContext const& ctx,
  std::span<appctx::EncodingStatePtr const> items,
  std::size_t overallTotalCount,
  std::size_t initialCompletedCount
) {
  LOG_INFO(
    "Preparing encoding batch: pending={} overall={} completed-before-start={} "
    "output-format={} pack-output={}",
    items.size(),
    overallTotalCount,
    initialCompletedCount,
    ctx.config.outputFormat,
    ctx.config.packOutput
  );
}

// Prompts before encoding starts; false when the user declined.
bool confirmEncodingStart(appctx::AppContext& ctx) {
  auto const proceed = readUserIpt(
    ctx.config.yesToAll,
    std::format(
      "do you want to encode the video to {} format? (Y/n): ",
      terminal::accent(ctx.config.outputFormat)
    )
  );
  if (!proceed) {
    terminal::messageln(Warning, "Encoding tasks canceled by user.");
    LOG_INFO("Encoding canceled by user.");
    return false;
  }
  return true;
}

auto videobatch::runEncodingTasks(
  appctx::AppContext& ctx,
  std::span<appctx::EncodingStatePtr const> items,
  std::size_t overallTotalCount,
  std::size_t initialCompletedCount
) -> EncodingBatchSummary {
  if (items.empty()) { return EncodingBatchSummary{}; }
  logBatchStart(ctx, items, overallTotalCount, initialCompletedCount);

  // Pre-encode quality probing (MP4 only; --crf bypasses it entirely): picks
  // a per-file CQ meeting the quality floor and prints the plan before the
  // confirmation prompt. A stop request during probing aborts the run.
  auto attentionWarnings = std::vector<std::string>{};
  auto encodableItems =
    std::vector<appctx::EncodingStatePtr>{};  // filled by runProbeStage
  switch (runProbeStage(ctx, items, encodableItems, attentionWarnings)) {
    case ProbeStageStatus::Proceed: break;
    case ProbeStageStatus::DryRun:
      return EncodingBatchSummary{
        .attentionWarnings = std::move(attentionWarnings),
        .dryRun = true,
      };
    case ProbeStageStatus::Aborted:
    case ProbeStageStatus::Failed : return EncodingBatchSummary{.canceled = true};
  }

  if (!confirmEncodingStart(ctx)) { return EncodingBatchSummary{.canceled = true}; }

  // The encode phase's own clock starts after the prompt: waiting for the
  // user is not encode time.
  auto const encodeStartedAt = std::chrono::steady_clock::now();
  auto const encodeElapsedNow = [&] {
    return displaytext::elapsedSince(encodeStartedAt);
  };

  // Skipped (too-large estimate) files count as completed up front so the
  // overall bar reaches its total when the remaining encodes finish.
  auto const skippedBeforeStart = items.size() - encodableItems.size();

  if (ctx.config.verbose) {
    runVerboseEncoding(ctx, encodableItems);
    return EncodingBatchSummary{
      .attentionWarnings = std::move(attentionWarnings),
      .skippedCount = skippedBeforeStart,
      .encodeElapsed = encodeElapsedNow(),
    };
  }

  if (encodableItems.empty()) {
    return EncodingBatchSummary{
      .attentionWarnings = std::move(attentionWarnings),
      .skippedCount = skippedBeforeStart,
      .encodeElapsed = encodeElapsedNow(),
    };
  }

  auto execution = prepareEncodingExecution(
    ctx,
    encodableItems,
    overallTotalCount,
    initialCompletedCount + skippedBeforeStart
  );

  // The runner hands each task its item by reference; the flow needs the
  // handle it owns (the monitor's active slot) and the item's position (its
  // log line), both of which the batch list already holds.
  auto positions = std::unordered_map<appctx::EncodingState const*, std::size_t>{};
  positions.reserve(encodableItems.size());
  for (auto position = std::size_t{0}; position < encodableItems.size(); ++position) {
    positions.emplace(encodableItems[position].get(), position);
  }

  // The flow draws the two-tier bar layout itself (Overall plus one bar per
  // worker slot). The stage drives the Overall bar but not its value: the
  // flow's postfix owns that value, because it counts with the flow's own
  // denominator (probe-skipped items are done before the stage starts, and
  // each running encode contributes its partial progress).
  auto const overallBarIndex = execution.ctx->counters().overallBarIndex;
  // With the Overall bar absent the stage drives no bar of its own; the slot
  // bars the monitor paints still need the cursor hidden.
  auto cursorGuard = std::optional<progress::CursorGuard>{};
  if (!overallBarIndex.has_value()) { cursorGuard.emplace(); }

  auto const stageResult = mediaitem::runStage(
    mediaitem::StageSpec{
      .progress = overallBarIndex.has_value() ? &execution.ctx->progress() : nullptr,
      .barIndex = overallBarIndex.value_or(0),
      .setBarProgress = false,
      .maxConcurrency = execution.maxConcurrentJobs,
      .postfix =
        [&execution](std::size_t, std::size_t, double) {
          execution.ctx->markFinished();
          execution.ctx->updateOverall();
          return execution.ctx->overallText();
        },
    },
    std::span<appctx::EncodingStatePtr const>{encodableItems},
    [](appctx::EncodingState const&) { return false; },
    [&](appctx::EncodingState& item, taskexec::TaskContext& taskCtx) -> eh::Result<void> {
      auto const position = positions.at(&item);
      try {
        return runEncodingTask(
          *execution.ctx,
          encodableItems[position],
          position,
          taskCtx.slot
        );
      } catch (std::exception const& ex) {
        recordTaskException(*execution.ctx, taskCtx.slot, ex.what());
        throw;
      } catch (...) {
        recordTaskException(*execution.ctx, taskCtx.slot, "unknown exception");
        throw;
      }
    }
  );

  execution.monitorThread.join();

  // The batch is over on every path (success, cancel, failure): the phase
  // clears its own bars before anything else prints.
  execution.progressState->progressCtx.eraseBars();

  auto const completed = stageResult.attempted;

  LOG_INFO(
    "Encoding batch completed: attempted={} completed={} ",
    stageResult.attempted,
    completed
  );

  return EncodingBatchSummary{
    .attentionWarnings = std::move(attentionWarnings),
    .skippedCount = skippedBeforeStart,
    .encodeElapsed = encodeElapsedNow(),
  };
}
