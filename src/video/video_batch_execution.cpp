#include "video/video_batch_execution.h"

#include "video/video_encode_runner.h"
#include "video/video_workflow_utils.h"

#include "core/display_text.h"
#include "core/job_state.h"
#include "core/task_executor.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "utils/utils.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <thread>

namespace fs = std::filesystem;
using enum terminal::MessageKind;
using videobatch::detail::EncodingExecutionContext;
using videobatch::detail::EncodingProgressState;
using videoworkflow::lookupPlannedOutputFile;
using videoworkflow::withActionJobState;
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
  withActionJobState(
    ctx,
    actionId,
    [](jobstate::Store& store, std::string const& currentActionId) {
      store.markRunning(currentActionId);
    }
  );
}

auto finalizeEncodeResult(
  appctx::AppContext& ctx,
  std::optional<std::string> const& actionId,
  bool success,
  std::string const& failureReason
) -> void {
  withActionJobState(
    ctx,
    actionId,
    [&](jobstate::Store& store, std::string const& currentActionId) {
      if (success) {
        store.markSucceeded(currentActionId);
      } else {
        store.markFailed(currentActionId, failureReason);
      }
    }
  );
}

auto truncateForProgressLabel(std::string const& text, std::size_t maxLen = 48)
  -> std::string {
  return displaytext::truncateWithEllipsis(text, maxLen);
}

auto makeSlotLabel(fs::path const& vidPath) -> std::string {
  return truncateForProgressLabel(displaytext::pathToUtf8String(vidPath.filename()));
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
  withActionJobState(
    executionCtx.app,
    actionId,
    [&](jobstate::Store& store, std::string const& currentActionId) {
      store.markProgress(currentActionId, std::nullopt, std::nullopt, status);
    }
  );
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
  return vidState;
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

  spdlog::debug(
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
    withActionJobState(
      executionCtx.app,
      vidState->actionId,
      [](jobstate::Store& store, std::string const& currentActionId) {
        store.markRunning(currentActionId);
      }
    );
  }
  executionCtx.barEncodingStart(*vidState, fileLabel);
  auto const result =
    encodeVideo(executionCtx.app, *vidState, [&](std::string const& status) {
      reportEncodingStatus(executionCtx, *vidState, fileLabel, status);
    });

  executionCtx.finalizeState(vidState, result);

  auto outputFile = std::optional<fs::path>{};
  auto actionId = std::optional<std::string>{};
  auto lastStatus = std::optional<std::string>{};
  auto failureReason = std::string{"encoding failed"};
  auto elapsedMs = int64_t{0};
  {
    auto lock = std::scoped_lock{vidState->mtx};
    outputFile = vidState->outputFile;
    actionId = vidState->actionId;
    lastStatus = vidState->lastStatus;
    if (vidState->lastError.has_value()) {
      failureReason = vidState->lastError.value();
    } else if (vidState->lastStatus.has_value()) {
      failureReason = vidState->lastStatus.value();
    }
    if (vidState->startTime.has_value() && vidState->endTime.has_value()) {
      using namespace std::chrono;
      auto const elapsed = vidState->endTime.value() - vidState->startTime.value();
      elapsedMs = duration_cast<milliseconds>(elapsed).count();
    }
  }

  withActionJobState(
    executionCtx.app,
    actionId,
    [&](jobstate::Store& store, std::string const& currentActionId) {
      if (result) {
        if (lastStatus.has_value()) {
          store.markSucceeded(currentActionId, lastStatus.value());
        } else {
          store.markSucceeded(currentActionId);
        }
      } else {
        store.markFailed(currentActionId, failureReason);
      }
    }
  );

  if (result) {
    spdlog::info(
      "[slot:{} task:{}/{}] encoded success: {} -> {} ({} ms)",
      slot + 1,
      taskIndex + 1,
      executionCtx.pendingTotal(),
      vidPath.string(),
      outputFile.has_value() ? outputFile->string() : "<unknown>",
      elapsedMs
    );
  } else {
    spdlog::warn(
      "[slot:{} task:{}/{}] encoded failed: {} ({} ms)",
      slot + 1,
      taskIndex + 1,
      executionCtx.pendingTotal(),
      vidPath.string(),
      elapsedMs
    );
  }

  executionCtx.barIdle(barIndex, slot);
  executionCtx.clearActive(slot);

  executionCtx.markFinished();
  executionCtx.updateOverall();

  if (!result) { return eh::makeError("{}", failureReason); }
  return {};
}

auto runEncodingWithoutProgress(
  appctx::AppContext& ctx,
  std::vector<fs::path> const& vids,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  videobatch::ActionIdMap const& actionIds
) -> videobatch::EncodeResultsMap {
  auto vidsRunRes = videobatch::EncodeResultsMap{};

  spdlog::info(
    "Running encoding without progress bars (verbose echo mode), total={}.",
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

    spdlog::debug("Start encoding (no-progress): {}", vidPath.string());
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
      spdlog::info("Encoded success (no-progress): {}", vidPath.string());
    } else {
      spdlog::warn("Encoded failed (no-progress): {}", vidPath.string());
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
) -> std::optional<EncodeResultsMap> {
  constexpr auto kMaxConcurrentJobs = std::size_t{10};

  if (vids.empty()) { return EncodeResultsMap{}; }

  spdlog::info(
    "Preparing encoding batch: pending={} overall={} completed-before-start={} "
    "output-format={} pack-output={}",
    vids.size(),
    overallTotalCount,
    initialCompletedCount,
    ctx.config.outputFormat,
    ctx.config.packOutput
  );

  auto const proceed = readUserIpt(
    ctx.config.yesToAll,
    std::format(
      "do you want to encode the video to {} format? (Y/n): ",
      terminal::value("{}", ctx.config.outputFormat)
    )
  );
  if (!proceed) {
    terminal::println(Warning, "Encoding tasks canceled by user.");
    spdlog::info("Encoding canceled by user.");
    return std::nullopt;
  }

  if (ctx.config.verbose && ctx.config.verboseEcho) {
    terminal::println(Warning, "Verbose echo enabled: progress bars are disabled.");
    spdlog::debug("Progress bars disabled due to verbose echo mode.");
    return runEncodingWithoutProgress(ctx, vids, plannedOutputFiles, actionIds);
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
  spdlog::info(
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
  executionCtx.updateOverall();
  auto monitorThread = videobatch::detail::startEncodingMonitor(executionCtx);

  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(vids.size());
  for (auto taskIndex = std::size_t{0}; taskIndex < vids.size(); ++taskIndex) {
    tasks.push_back({
      .id = std::format("encode:{}", vids[taskIndex].string()),
      .label = vids[taskIndex].filename().string(),
      .run = [&, taskIndex, vidPath = vids[taskIndex]](taskexec::TaskContext& taskCtx) {
        return runEncodingTask(executionCtx, taskIndex, vidPath, taskCtx.slot);
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

  spdlog::info(
    "Encoding batch completed: attempted={} completed={} ",
    runState.attemptedCount,
    results.size()
  );

  return results;
}
