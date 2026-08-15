#include "video/video_batch_execution.h"

#include "video/encode_probe.h"
#include "video/video_encode_runner.h"
#include "video/video_workflow_utils.h"

#include "core/display_text.h"
#include "core/collision_naming.h"
#include "core/job_state.h"
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
#include <thread>

DEFINE_LOGGER(logtags::VIDEO_BATCH);

namespace fs = std::filesystem;
using enum terminal::MessageKind;
using videobatch::detail::EncodingExecutionContext;
using videobatch::detail::EncodingProgressState;
using videoworkflow::lookupPlannedOutputFile;
using videoworkflow::maybeJobState;
using videoworkflow::withJobState;

namespace {

void noteStopRequest(appctx::AppContext& ctx) {
  if (!stopsignal::isStopRequested()) { return; }
  withJobState(ctx, [](jobstate::Store& store) { store.requestCancel(); });
}

auto markRunningNoProgress(
  appctx::AppContext& ctx,
  std::optional<std::string> const& actionId
) -> void {
  if (auto* store = maybeJobState(ctx); actionId.has_value()) {
    store->markRunning(actionId.value());
  }
}

auto finalizeEncodeResult(
  appctx::AppContext& ctx,
  std::optional<std::string> const& actionId,
  bool success,
  std::string const& failureReason
) -> void {
  if (auto* store = maybeJobState(ctx); actionId.has_value()) {
    if (success) {
      store->markSucceeded(actionId.value());
    } else {
      store->markFailed(actionId.value(), failureReason);
    }
  }
}

auto makeSlotLabel(fs::path const& vidPath) -> std::string {
  return displaytext::pathToUtf8String(vidPath.filename());
}

auto reportEncodingStatus(
  EncodingExecutionContext& executionCtx,
  appctx::EncodingState& vidState,
  std::string const& fileLabel,
  std::string const& status
) -> void {
  executionCtx.barEncodingStatus(vidState, fileLabel, status);
  auto actionId = std::optional<std::string>{};
  auto lock = std::scoped_lock{vidState.mtx};
  vidState.lastStatus = status;
  actionId = vidState.actionId;
  if (auto* store = maybeJobState(executionCtx.app); actionId.has_value()) {
    store->markProgress(actionId.value(), std::nullopt, std::nullopt, status);
  }
}

auto createEncodingState(
  EncodingExecutionContext& executionCtx,
  fs::path const& vidPath,
  std::optional<std::size_t> barIndex
) -> appctx::EncodingStatePtr {
  auto vidState = std::make_shared<appctx::EncodingState>();
  vidState->inputPath = vidPath;
  vidState->barIndex = barIndex;
  if (auto const* actionId = executionCtx.actionIds.find(vidPath); actionId != nullptr) {
    vidState->actionId = *actionId;
  }
  vidState->startTime = std::chrono::steady_clock::now();
  vidState->plannedOutputFile =
    lookupPlannedOutputFile(executionCtx.plannedOutputFiles, vidPath);
  if (vidState->plannedOutputFile.has_value()) {
    vidState->outputPath = vidState->plannedOutputFile->parent_path();
  }
  if (
    auto const it = executionCtx.probeCqByInput.find(vidPath);
    it != executionCtx.probeCqByInput.end()
  ) {
    vidState->chosenCq = it->second;
  }
  return vidState;
}

// Records the message of an exception that escaped runEncodingTask (which
// would otherwise leave no specific failure reason and a stale active slot)
// onto the slot's encoding state, then clears the slot.
auto recordTaskException(
  EncodingExecutionContext& executionCtx,
  std::size_t slot,
  std::string_view message
) -> void {
  auto const state = executionCtx.activeState(slot);
  if (state) {
    auto lock = std::scoped_lock{state->mtx};
    state->lastError = std::string{message};
  }
  executionCtx.clearActive(slot);
}

struct EncodingOutcome {
  std::optional<fs::path> outputFile_;
  std::optional<std::string> actionId_;
  std::optional<std::string> lastStatus_;
  std::string failureReason_ = "encoding failed";
  int64_t elapsedMs_ = 0;
};

auto collectOutcome(appctx::EncodingState& vidState) -> EncodingOutcome {
  auto outcome = EncodingOutcome{};
  auto lock = std::scoped_lock{vidState.mtx};
  outcome.outputFile_ = vidState.outputFile;
  outcome.actionId_ = vidState.actionId;
  outcome.lastStatus_ = vidState.lastStatus;
  if (vidState.lastError.has_value()) {
    outcome.failureReason_ = vidState.lastError.value();
  } else if (vidState.lastStatus.has_value()) {
    outcome.failureReason_ = vidState.lastStatus.value();
  }
  if (vidState.startTime.has_value() && vidState.endTime.has_value()) {
    using namespace std::chrono;
    auto const elapsed = vidState.endTime.value() - vidState.startTime.value();
    outcome.elapsedMs_ = duration_cast<milliseconds>(elapsed).count();
  }
  return outcome;
}

void notifyJobState(
  appctx::AppContext& ctx,
  bool result,
  EncodingOutcome const& outcome
) {
  if (auto* store = maybeJobState(ctx); outcome.actionId_.has_value()) {
    if (result) {
      if (outcome.lastStatus_.has_value()) {
        store->markSucceeded(outcome.actionId_.value(), outcome.lastStatus_.value());
      } else {
        store->markSucceeded(outcome.actionId_.value());
      }
    } else {
      store->markFailed(outcome.actionId_.value(), outcome.failureReason_);
    }
  }
}

auto runEncodingTask(
  EncodingExecutionContext& executionCtx,
  std::size_t taskIndex,
  fs::path const& vidPath,
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
    vidPath.string()
  );
  auto const barIndex = executionCtx.barIndexOpt(slot);
  auto vidState = createEncodingState(executionCtx, vidPath, barIndex);
  executionCtx.setActive(slot, vidState);

  auto const fileLabel = makeSlotLabel(vidPath);
  {
    auto lock = std::scoped_lock{vidState->mtx};
    if (auto* store = maybeJobState(executionCtx.app); vidState->actionId.has_value()) {
      store->markRunning(vidState->actionId.value());
    }
  }
  executionCtx.barEncodingStart(*vidState, fileLabel);
  auto const result =
    encodeVideo(executionCtx.app, *vidState, [&](std::string const& status) {
      reportEncodingStatus(executionCtx, *vidState, fileLabel, status);
    });

  executionCtx.finalizeState(vidState, result);

  auto const outcome = collectOutcome(*vidState);
  notifyJobState(executionCtx.app, result, outcome);

  if (result) {
    LOG_INFO(
      "[slot:{} task:{}/{}] encoded success: {} -> {} ({} ms)",
      slot + 1,
      taskIndex + 1,
      executionCtx.pendingTotal(),
      vidPath.string(),
      outcome.outputFile_.has_value() ? outcome.outputFile_->string() : "<unknown>",
      outcome.elapsedMs_
    );
  } else {
    LOG_WARN(
      "[slot:{} task:{}/{}] encoded failed: {} ({} ms)",
      slot + 1,
      taskIndex + 1,
      executionCtx.pendingTotal(),
      vidPath.string(),
      outcome.elapsedMs_
    );
  }

  executionCtx.barDone(barIndex, result, fileLabel);
  executionCtx.clearActive(slot);

  executionCtx.markFinished();
  executionCtx.updateOverall();

  if (!result) { return eh::makeError("{}", outcome.failureReason_); }
  return {};
}

auto runEncodingWithoutProgress(
  appctx::AppContext& ctx,
  std::vector<fs::path> const& vids,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  videobatch::ActionIdMap const& actionIds,
  appctx::path_map<int> const& probeCqByInput
) -> videobatch::EncodeResultsMap {
  auto vidsRunRes = videobatch::EncodeResultsMap{};

  LOG_INFO(
    "Running encoding without progress bars (verbose output mode), total={}.",
    vids.size()
  );

  for (auto const& vidPath: vids) {
    if (stopsignal::isStopRequested()) {
      noteStopRequest(ctx);
      break;
    }

    auto state = appctx::EncodingState{};
    state.inputPath = vidPath;
    if (auto const* actionId = actionIds.find(vidPath); actionId != nullptr) {
      state.actionId = *actionId;
    }
    state.plannedOutputFile = lookupPlannedOutputFile(plannedOutputFiles, vidPath);
    if (state.plannedOutputFile.has_value()) {
      state.outputPath = state.plannedOutputFile->parent_path();
    }
    if (auto const it = probeCqByInput.find(vidPath); it != probeCqByInput.end()) {
      state.chosenCq = it->second;
    }

    LOG_DEBUG("Start encoding (no-progress): {}", vidPath.string());
    auto const taskId =
      state.actionId
        .value_or(std::format("encode:{}", collisionnaming::stablePathString(vidPath)));
    // Same correlation as the executor path: task_id + input on every record
    auto const vidPathText = vidPath.string();
    auto attrs = logging::ScopedLogAttributes(
      {{std::string_view{"task_id"}, taskId}, {std::string_view{"input"}, vidPathText}}
    );
    markRunningNoProgress(ctx, state.actionId);

    auto const success = encodeVideo(ctx, state, {});
    if (state.progressFilePath.has_value()) {
      auto ec = std::error_code{};
      fs::remove(state.progressFilePath.value(), ec);
    }
    vidsRunRes = vidsRunRes.set(vidPath, success);
    finalizeEncodeResult(
      ctx,
      state.actionId,
      success,
      state.lastError.value_or("encoding failed")
    );
    if (success) {
      LOG_INFO("Encoded success (no-progress): {}", vidPath.string());
    } else {
      LOG_WARN("Encoded failed (no-progress): {}", vidPath.string());
    }
  }

  return vidsRunRes;
}

}  // namespace

auto videobatch::runEncodingTasks(
  appctx::AppContext& ctx,
  std::vector<fs::path> const& vids,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  ActionIdMap const& actionIds,
  std::size_t overallTotalCount,
  std::size_t initialCompletedCount
) -> EncodingBatchOutcome {
  constexpr auto kMaxConcurrentJobs = std::size_t{10};

  if (vids.empty()) { return EncodingBatchOutcome{.results = EncodeResultsMap{}}; }

  LOG_INFO(
    "Preparing encoding batch: pending={} overall={} completed-before-start={} "
    "output-format={} pack-output={}",
    vids.size(),
    overallTotalCount,
    initialCompletedCount,
    ctx.config.outputFormat,
    ctx.config.packOutput
  );

  // Pre-encode quality probing (MP4 only; --crf bypasses it entirely): picks
  // a per-file CQ meeting the quality floor and prints the plan before the
  // confirmation prompt. A stop request during probing aborts the run.
  auto attentionWarnings = std::vector<std::string>{};
  auto probeCqByInput = appctx::path_map<int>{};
  auto const shouldProbe =
    ctx.config.outputFormat == "mp4" && !ctx.config.crf.has_value();
  if (shouldProbe) {
    auto const probeRes = encodeprobe::runProbePhase(ctx, vids);
    if (stopsignal::isStopRequested()) {
      noteStopRequest(ctx);
      return EncodingBatchOutcome{.results = std::nullopt};
    }
    if (!probeRes) {
      LOG_ERROR("{}", probeRes.error());
      terminal::println(Error, "Error: {}", probeRes.error());
      return EncodingBatchOutcome{.results = std::nullopt};
    }
    for (auto const& [vidPath, plan]: probeRes->plans) {
      probeCqByInput[vidPath] = plan.chosenCq;
    }
    attentionWarnings = std::move(probeRes->attentionWarnings);

    auto plans = std::vector<encodeprobe::ProbePlan>{};
    plans.reserve(probeRes->plans.size());
    for (auto const& vidPath: vids) {
      if (auto const it = probeRes->plans.find(vidPath); it != probeRes->plans.end()) {
        plans.push_back(it->second);
      }
    }
    encodeprobe::printProbePlan(plans, ctx.config.minVmaf);

    if (ctx.config.dryRun) {
      LOG_INFO("Dry run: probe plan printed; exiting without encoding.");
      return EncodingBatchOutcome{
        .results = EncodeResultsMap{},
        .attentionWarnings = std::move(attentionWarnings),
        .dryRun = true,
      };
    }
  }

  auto const proceed = readUserIpt(
    ctx.config.yesToAll,
    std::format(
      "do you want to encode the video to {} format? (Y/n): ",
      terminal::value("{}", ctx.config.outputFormat)
    )
  );
  if (!proceed) {
    terminal::println(Warning, "Encoding tasks canceled by user.");
    LOG_INFO("Encoding canceled by user.");
    return EncodingBatchOutcome{.results = std::nullopt};
  }

  if (ctx.config.verbose) {
    terminal::println(Warning, "Verbose output enabled: progress bars are disabled.");
    auto const results = runEncodingWithoutProgress(
      ctx,
      vids,
      plannedOutputFiles,
      actionIds,
      probeCqByInput
    );
    return EncodingBatchOutcome{
      .results = results,
      .attentionWarnings = std::move(attentionWarnings),
    };
  }

  auto const maxConcurrentJobs =
    std::max<std::size_t>(1, ctx.config.maxParallelJobs.value_or(kMaxConcurrentJobs));
  auto const workerCount = taskexec::resolveWorkerCount(vids.size(), maxConcurrentJobs);
  auto const compact = !ctx.config.fullProgress;
  auto progressState = EncodingProgressState{
    vids.size(),
    overallTotalCount,
    initialCompletedCount,
    workerCount,
    compact
  };

  terminal::println(
    Info,
    "Scheduling {} video(s) with max {} concurrent encode job(s)...",
    terminal::count(vids.size()),
    terminal::count(workerCount)
  );
  LOG_INFO(
    "Scheduling encoding workers: workers={} pending={} overall={} "
    "completed-before-start={}",
    workerCount,
    vids.size(),
    overallTotalCount,
    initialCompletedCount
  );

  auto executionCtx = EncodingExecutionContext{
    ctx,
    progressState,
    plannedOutputFiles,
    actionIds,
  };
  executionCtx.probeCqByInput = std::move(probeCqByInput);
  executionCtx.updateOverall();

  logging::setForensicAppContext(&ctx);
  struct ForensicContextGuard {
    ~ForensicContextGuard() { logging::setForensicAppContext(nullptr); }
  };
  auto forensicGuard = ForensicContextGuard{};

  auto monitorThread = videobatch::detail::startEncodingMonitor(executionCtx);

  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(vids.size());
  for (auto taskIndex = std::size_t{0}; taskIndex < vids.size(); ++taskIndex) {
    tasks.push_back({
      .id = std::format("encode:{}", collisionnaming::stablePathString(vids[taskIndex])),
      .label = vids[taskIndex].filename().string(),
      .input = vids[taskIndex].string(),
      .run = [&, taskIndex, vidPath = vids[taskIndex]](taskexec::TaskContext& taskCtx) {
        try {
          return runEncodingTask(executionCtx, taskIndex, vidPath, taskCtx.slot);
        } catch (std::exception const& ex) {
          recordTaskException(executionCtx, taskCtx.slot, ex.what());
          throw;
        } catch (...) {
          recordTaskException(executionCtx, taskCtx.slot, "unknown exception");
          throw;
        }
      },
    });
  }

  auto const runState = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = maxConcurrentJobs,
    .progress = &progressState.progressCtx,
    .hideCursor = true,
  });

  monitorThread.join();

  auto results = EncodeResultsMap{};
  for (auto taskIndex = std::size_t{0}; taskIndex < vids.size(); ++taskIndex) {
    if (runState.attempted[taskIndex] == 0) { continue; }
    results = results.set(vids[taskIndex], runState.results[taskIndex].has_value());
  }

  LOG_INFO(
    "Encoding batch completed: attempted={} completed={} ",
    runState.attemptedCount,
    results.size()
  );

  return EncodingBatchOutcome{
    .results = results,
    .attentionWarnings = std::move(attentionWarnings),
  };
}
