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

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::VIDEO_BATCH);

namespace fs = std::filesystem;
using enum terminal::MessageKind;
using videobatch::detail::EncodingExecutionContext;
using videobatch::detail::EncodingProgressState;
using videoworkflow::lookupPlannedOutputFile;
using videoworkflow::maybeJobState;
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

void noteStopRequest(appctx::AppContext& ctx) {
  if (!stopsignal::isStopRequested()) { return; }
  withJobState(ctx, [](jobstate::Store& store) { store.requestCancel(); });
}

void markRunningNoProgress(
  appctx::AppContext& ctx,
  std::optional<std::string> const& actionId
) {
  if (auto* store = maybeJobState(ctx); actionId.has_value()) {
    store->markRunning(actionId.value());
  }
}

void finalizeEncodeResult(
  appctx::AppContext& ctx,
  std::optional<std::string> const& actionId,
  bool success,
  std::string const& failureReason
) {
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

auto createEncodingState(
  EncodingExecutionContext& executionCtx,
  fs::path const& vidPath,
  std::optional<std::size_t> barIndex
) -> appctx::EncodingStatePtr {
  auto vidState = std::make_shared<appctx::EncodingState>();
  vidState->inputPath = vidPath;
  vidState->barIndex = barIndex;
  if (
    auto const it = executionCtx.actionIds.find(vidPath);
    it != executionCtx.actionIds.end()
  ) {
    vidState->actionId = it->second;
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
  auto elapsedBase = std::chrono::milliseconds{0};
  if (auto* store = maybeJobState(executionCtx.app); store != nullptr) {
    elapsedBase = videobatch::detail::persistedElapsedMs(*store, vidState->actionId);
  }
  executionCtx.barEncodingStart(*vidState, fileLabel, elapsedBase);
  auto const result = encodeVideo(
    executionCtx.app,
    *vidState,
    [&](std::string const& status) {
      reportEncodingStatus(executionCtx, *vidState, fileLabel, status);
    },
    executionCtx.counters().workers
  );

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
  videobatch::EncodingBatchJob const& job,
  appctx::path_map<int> const& probeCqByInput
) -> videobatch::EncodeResultsMap {
  auto vidsRunRes = videobatch::EncodeResultsMap{};

  LOG_INFO(
    "Running encoding without progress bars (verbose output mode), total={}.",
    job.vids.size()
  );

  for (auto const& vidPath: job.vids) {
    if (stopsignal::isStopRequested()) {
      noteStopRequest(ctx);
      break;
    }

    auto state = appctx::EncodingState{};
    state.inputPath = vidPath;
    if (auto const it = job.actionIds.find(vidPath); it != job.actionIds.end()) {
      state.actionId = it->second;
    }
    state.plannedOutputFile = lookupPlannedOutputFile(job.plannedOutputFiles, vidPath);
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
    vidsRunRes.emplace(vidPath, success);
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

namespace {

enum class ProbeStageStatus {
  Proceed,
  Aborted,
  Failed,
  DryRun
};

// Probing stage of runEncodingTasks (MP4 only; --crf bypasses it entirely):
// picks a per-file CQ meeting the quality floor and prints the plan before
// the confirmation prompt. Fills probeCqByInput/attentionWarnings on
// success; encodableVids receives the videos that survive the probe (plans
// whose estimated size exceeds the source are dropped).
auto runProbeStage(
  appctx::AppContext& ctx,
  std::vector<fs::path> const& vids,
  std::vector<fs::path>& encodableVids,
  appctx::path_map<int>& probeCqByInput,
  std::vector<std::string>& attentionWarnings
) -> ProbeStageStatus {
  auto const shouldProbe =
    ctx.config.outputFormat == "mp4" && !ctx.config.crf.has_value();
  if (!shouldProbe) {
    encodableVids.assign(vids.begin(), vids.end());
    return ProbeStageStatus::Proceed;
  }

  auto probeRes = encodeprobe::runProbePhase(ctx, vids);
  if (stopsignal::isStopRequested()) {
    noteStopRequest(ctx);
    return ProbeStageStatus::Aborted;
  }
  if (!probeRes) {
    LOG_ERROR("{}", probeRes.error());
    terminal::println(Error, "Error: {}", probeRes.error());
    return ProbeStageStatus::Failed;
  }
  for (auto const& [vidPath, plan]: probeRes->plans) {
    probeCqByInput[vidPath] = plan.chosenCq;
  }
  // Plans marked skipEncode (estimated output > source) never reach the
  // encode stage; the printed plan flags them as skipped.
  encodableVids.clear();
  for (auto const& vidPath: vids) {
    auto const it = probeRes->plans.find(vidPath);
    if (it != probeRes->plans.end() && it->second.skipEncode) { continue; }
    encodableVids.push_back(vidPath);
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
    return ProbeStageStatus::DryRun;
  }
  return ProbeStageStatus::Proceed;
}

auto buildEncodeTasks(
  std::vector<fs::path> const& vids,
  EncodingExecutionContext& executionCtx
) -> std::vector<taskexec::TaskSpec> {
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
  return tasks;
}

auto collectEncodingResults(
  std::vector<fs::path> const& vids,
  taskexec::TaskRunResult const& runState
) -> videobatch::EncodeResultsMap {
  auto results = videobatch::EncodeResultsMap{};
  for (auto taskIndex = std::size_t{0}; taskIndex < vids.size(); ++taskIndex) {
    if (runState.attempted[taskIndex] == 0) { continue; }
    results.emplace(vids[taskIndex], runState.results[taskIndex].has_value());
  }
  return results;
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

auto runVerboseEncoding(
  appctx::AppContext& ctx,
  videobatch::EncodingBatchJob const& job,
  appctx::path_map<int>& probeCqByInput
) -> videobatch::EncodeResultsMap {
  terminal::println(Warning, "Verbose output enabled: progress bars are disabled.");
  return runEncodingWithoutProgress(ctx, job, probeCqByInput);
}

auto prepareEncodingExecution(
  appctx::AppContext& ctx,
  videobatch::EncodingBatchJob const& job,
  std::size_t overallTotalCount,
  std::size_t initialCompletedCount,
  appctx::path_map<int>& probeCqByInput
) -> PreparedEncodingExecution {
  constexpr auto kMaxConcurrentJobs = std::size_t{10};
  auto const maxConcurrentJobs =
    std::max<std::size_t>(1, ctx.config.maxParallelJobs.value_or(kMaxConcurrentJobs));
  auto const workerCount =
    taskexec::resolveWorkerCount(job.vids.size(), maxConcurrentJobs);
  auto const compact = !ctx.config.fullProgress;
  auto progressState = std::make_unique<
    EncodingProgressState
  >(job.vids.size(), overallTotalCount, initialCompletedCount, workerCount, compact);

  terminal::println(
    Info,
    "Scheduling {} video(s) with max {} concurrent encode job(s)...",
    terminal::count(job.vids.size()),
    terminal::count(workerCount)
  );
  LOG_INFO(
    "Scheduling encoding workers: workers={} pending={} overall={} "
    "completed-before-start={}",
    workerCount,
    job.vids.size(),
    overallTotalCount,
    initialCompletedCount
  );

  auto executionCtx = std::make_unique<
    EncodingExecutionContext
  >(ctx, *progressState, job.plannedOutputFiles, job.actionIds);
  executionCtx->probeCqByInput = std::move(probeCqByInput);
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
  std::vector<fs::path> const& vids,
  std::size_t overallTotalCount,
  std::size_t initialCompletedCount
) {
  LOG_INFO(
    "Preparing encoding batch: pending={} overall={} completed-before-start={} "
    "output-format={} pack-output={}",
    vids.size(),
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
      terminal::value("{}", ctx.config.outputFormat)
    )
  );
  if (!proceed) {
    terminal::println(Warning, "Encoding tasks canceled by user.");
    LOG_INFO("Encoding canceled by user.");
    return false;
  }
  return true;
}

auto videobatch::runEncodingTasks(
  appctx::AppContext& ctx,
  videobatch::EncodingBatchJob const& job,
  std::size_t overallTotalCount,
  std::size_t initialCompletedCount
) -> EncodingBatchOutcome {
  if (job.vids.empty()) { return EncodingBatchOutcome{.results = EncodeResultsMap{}}; }
  logBatchStart(ctx, job.vids, overallTotalCount, initialCompletedCount);

  // Pre-encode quality probing (MP4 only; --crf bypasses it entirely): picks
  // a per-file CQ meeting the quality floor and prints the plan before the
  // confirmation prompt. A stop request during probing aborts the run.
  auto attentionWarnings = std::vector<std::string>{};
  auto probeCqByInput = appctx::path_map<int>{};
  auto encodableVids = std::vector<fs::path>{};  // filled by runProbeStage
  switch (
    runProbeStage(ctx, job.vids, encodableVids, probeCqByInput, attentionWarnings)) {
    case ProbeStageStatus::Proceed: break;
    case ProbeStageStatus::DryRun:
      return EncodingBatchOutcome{
        .results = EncodeResultsMap{},
        .attentionWarnings = std::move(attentionWarnings),
        .dryRun = true,
      };
    case ProbeStageStatus::Aborted:
    case ProbeStageStatus::Failed : return EncodingBatchOutcome{.results = std::nullopt};
  }

  if (!confirmEncodingStart(ctx)) {
    return EncodingBatchOutcome{.results = std::nullopt};
  }

  // Skipped (too-large estimate) files count as completed up front so the
  // overall bar reaches its total when the remaining encodes finish.
  auto const skippedBeforeStart = job.vids.size() - encodableVids.size();
  auto const encodableJob = EncodingBatchJob{
    .vids = std::move(encodableVids),
    .plannedOutputFiles = job.plannedOutputFiles,
    .actionIds = job.actionIds,
  };

  if (ctx.config.verbose) {
    return EncodingBatchOutcome{
      .results = runVerboseEncoding(ctx, encodableJob, probeCqByInput),
      .attentionWarnings = std::move(attentionWarnings),
    };
  }

  if (encodableJob.vids.empty()) {
    return EncodingBatchOutcome{
      .results = EncodeResultsMap{},
      .attentionWarnings = std::move(attentionWarnings),
    };
  }

  auto execution = prepareEncodingExecution(
    ctx,
    encodableJob,
    overallTotalCount,
    initialCompletedCount + skippedBeforeStart,
    probeCqByInput
  );

  auto const runState = taskexec::runTasks({
    .tasks = buildEncodeTasks(encodableJob.vids, *execution.ctx),
    .maxConcurrency = execution.maxConcurrentJobs,
    .progress = &execution.progressState->progressCtx,
    .hideCursor = true,
  });

  execution.monitorThread.join();

  auto const results = collectEncodingResults(encodableJob.vids, runState);

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
