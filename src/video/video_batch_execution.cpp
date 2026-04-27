#include "video/video_batch_execution.h"

#include "video/video_encode_runner.h"
#include "video/video_progress_parser.h"
#include "video/video_workflow_utils.h"

#include "core/display_text.h"
#include "core/job_state.h"
#include "core/progress.h"
#include "core/task_executor.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "utils/utils.h"
#include "video/video_info.h"

#include <immer/atom.hpp>
#include <immer/vector.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <thread>

namespace fs = std::filesystem;
using enum terminal::MessageKind;
using videoworkflow::lookupPlannedOutputFile;
using videoworkflow::withActionJobState;
using videoworkflow::withJobState;

namespace {

void noteStopRequest(appctx::AppContext& ctx) {
  if (!stopsignal::isStopRequested()) { return; }
  withJobState(ctx, [](jobstate::Store& store) { store.requestCancel(); });
}

auto truncateForProgressLabel(std::string const& text, std::size_t maxLen = 48)
  -> std::string {
  return displaytext::truncateWithEllipsis(text, maxLen);
}

auto makeSlotLabel(fs::path const& vidPath) -> std::string {
  return truncateForProgressLabel(displaytext::pathToUtf8String(vidPath.filename()));
}

auto getStateLabel(appctx::EncodingState const& state) -> std::string {
  return truncateForProgressLabel(
    displaytext::pathToUtf8String(state.inputPath.filename())
  );
}

struct EncodingProgressState {
  using ActiveSlots = immer::vector<appctx::EncodingStatePtr>;

  struct SharedSnapshot {
    ActiveSlots active;
  };

  struct Counters {
    std::atomic_size_t finished;
    std::size_t pendingTotal;
    std::size_t overallTotal;
    std::size_t workers;
    std::optional<std::size_t> overallBarIndex;
  } counters;

  immer::atom<SharedSnapshot> snapshot;

  struct Slots {
    std::vector<std::size_t> barIndexes;
  } slots;

  progress::ProgressContext progressCtx;

  EncodingProgressState(std::size_t total, std::size_t workers)
    : EncodingProgressState(total, total, 0, workers) { }

  EncodingProgressState(
    std::size_t pendingTotal,
    std::size_t overallTotal,
    std::size_t completedBeforeStart,
    std::size_t workers,
    bool compact = false
  )
    : counters{
        std::atomic_size_t{std::min(completedBeforeStart, overallTotal)},
        pendingTotal,
        overallTotal,
        workers,
        std::nullopt
      },
      snapshot{makeInitialSnapshot(workers)},
      slots{
        std::vector<std::size_t>{},
      },
      progressCtx{} {
    counters.overallBarIndex =
      createOverallBar(progressCtx, overallTotal, completedBeforeStart, workers, compact);
    slots.barIndexes = makeSlotBars(progressCtx, workers, compact, overallTotal);
  }

private:
  static auto makeInitialSnapshot(std::size_t workerCount) -> SharedSnapshot {
    auto active = ActiveSlots{};
    for (auto slot = std::size_t{0}; slot < workerCount; ++slot) {
      active = active.push_back(appctx::EncodingStatePtr{});
    }

    return SharedSnapshot{std::move(active)};
  }

  static auto createOverallBar(
    progress::ProgressContext& progressCtx,
    std::size_t totalTasks,
    std::size_t completedBeforeStart,
    std::size_t workerCount,
    bool compact
  ) -> std::optional<std::size_t> {
    bool const showOverall = compact ? (totalTasks > 1) : (totalTasks > workerCount);
    if (!showOverall) { return std::optional<std::size_t>{}; }
    return std::optional<std::size_t>{progressCtx.addBar(
      std::format(
        "Overall: {}/{}",
        std::min(completedBeforeStart, totalTasks),
        totalTasks
      ),
      progress::Tone::Overall
    )};
  }

  static std::vector<std::size_t>
  makeSlotBars(progress::ProgressContext& progressCtx, std::size_t workerCount, bool compact, std::size_t totalTasks) {
    if (compact && totalTasks > 1) { return {}; }
    auto barIndexes = std::vector<std::size_t>(workerCount);
    for (auto slot = std::size_t{0}; slot < workerCount; ++slot) {
      barIndexes[slot] = progressCtx.addBar(
        std::format("Encoding: [idle-{}]", slot + 1),
        progress::Tone::Idle
      );
    }
    return barIndexes;
  }
};

struct EncodingExecutionContext {
  appctx::AppContext& app;
  EncodingProgressState& progressState;
  appctx::path_map<fs::path> const& plannedOutputFiles;
  videobatch::ActionIdMap const& actionIds;

  auto& counters() { return progressState.counters; }
  auto const& counters() const { return progressState.counters; }
  auto& slots() { return progressState.slots; }
  auto const& slots() const { return progressState.slots; }
  auto& progress() { return progressState.progressCtx; }
  auto const& progress() const { return progressState.progressCtx; }
  auto loadShared() const { return progressState.snapshot.load(); }

  auto pendingTotal() const { return counters().pendingTotal; }

  auto overallTotal() const { return counters().overallTotal; }

  auto finished() const { return counters().finished.load(std::memory_order_acquire); }

  void markFinished() { counters().finished.fetch_add(1, std::memory_order_release); }

  auto barIndex(std::size_t slot) const { return slots().barIndexes[slot]; }

  auto barIndexOpt(std::size_t slot) const -> std::optional<std::size_t> {
    if (slots().barIndexes.empty()) { return std::nullopt; }
    return slots().barIndexes[slot];
  }

  void setActive(std::size_t slot, appctx::EncodingStatePtr const& vidState) {
    progressState.snapshot.update(
      [=](EncodingProgressState::SharedSnapshot const& shared) {
        return EncodingProgressState::SharedSnapshot{
          .active = shared.active.set(slot, vidState),
        };
      }
    );
  }

  void clearActive(std::size_t slot) {
    progressState.snapshot.update(
      [=](EncodingProgressState::SharedSnapshot const& shared) {
        return EncodingProgressState::SharedSnapshot{
          .active = shared.active.set(slot, appctx::EncodingStatePtr{}),
        };
      }
    );
  }

  auto activeStates() -> appctx::EncodingStateList {
    auto activeStates = appctx::EncodingStateList{};
    auto const shared = loadShared();
    activeStates.reserve(shared->active.size());
    for (auto const& activeState: shared->active) {
      if (activeState) { activeStates.push_back(activeState); }
    }
    return activeStates;
  }

  void barEncodingStart(appctx::EncodingState& vidState, std::string_view fileLabel) {
    if (!vidState.barIndex.has_value()) { return; }
    auto const index = vidState.barIndex.value();
    progress().setTone(index, progress::Tone::Active);
    progress().setPostfixText(index, std::format("Encoding: {}", fileLabel));
    progress().setProgress(index, 0.0f);
  }

  void barEncodingStatus(
    appctx::EncodingState& vidState,
    std::string_view fileLabel,
    std::string_view status
  ) {
    if (!vidState.barIndex.has_value()) { return; }
    auto const index = vidState.barIndex.value();
    progress().setTone(index, progress::Tone::Active);
    progress().setPostfixText(index, std::format("Encoding: {} | {}", fileLabel, status));
  }

  void barIdle(std::optional<std::size_t> barIndex, std::size_t slot) {
    if (!barIndex.has_value()) { return; }
    progress().setTone(barIndex.value(), progress::Tone::Idle);
    progress().setProgress(barIndex.value(), 0.0f);
    progress().setPostfixText(barIndex.value(), std::format("Encoding: [idle-{}]", slot + 1));
  }

  void updateOverall() {
    if (!counters().overallBarIndex.has_value()) { return; }

    auto activeProgress = 0.0f;
    {
      auto const activeList = activeStates();
      for (auto const& activeState: activeList) {
        if (!activeState) { continue; }
        auto const p = activeState->lastProgressAtomic.load(std::memory_order_acquire);
        if (p >= 0.0f) { activeProgress += p / 100.0f; }
      }
    }

    auto const completed = finished();
    auto const totalCount = static_cast<float>(overallTotal());
    auto overallPercent = 0.0f;
    if (totalCount > 0.0f) {
      overallPercent =
        std::min(100.0f, (completed + activeProgress) / totalCount * 100.0f);
    }

    progress().setProgress(counters().overallBarIndex.value(), overallPercent);
    progress().setPostfixText(
      counters().overallBarIndex.value(),
      std::format("Overall: {}/{}", completed, overallTotal())
    );
  }

  void finalizeState(appctx::EncodingStatePtr const& vidState, bool result) {
    auto progressFileToRemove = std::optional<fs::path>{};

    {
      auto lock = std::scoped_lock{vidState->mtx};

      if (result) {
        if (
          vidState->plannedOutputFile.has_value()
          && fs::exists(vidState->plannedOutputFile.value())
        ) {
          vidState->outputFile = vidState->plannedOutputFile;
        }
      }

      vidState->finished = true;
      vidState->success = result;
      vidState->endTime = std::chrono::steady_clock::now();
      vidState->lastProgress = 100.0f;
      vidState->lastProgressAtomic.store(100.0f, std::memory_order_release);

      progressFileToRemove = vidState->progressFilePath;
    }

    if (progressFileToRemove.has_value()) {
      auto ec = std::error_code{};
      fs::remove(progressFileToRemove.value(), ec);
    }
  }
};

auto tryReadProgressData(fs::path const& progressFilePath)
  -> std::optional<ProgressData> {
  return parseProgressFile(progressFilePath);
}

auto getEncodingProgress(appctx::AppContext& ctx, appctx::EncodingState& state)
  -> std::optional<float> {
  if (!state.totalFrames.has_value()) {
    auto const totalFramesRes =
      getVidTotalFrames(ctx.toolchain, ctx.runtime, state.inputPath);
    if (!totalFramesRes.has_value()) {
      auto lock = std::scoped_lock{state.mtx};
      state.lastError = totalFramesRes.error();
      return std::nullopt;
    }
    state.totalFrames = totalFramesRes.value();
  }

  auto progressFilePath = std::optional<fs::path>{};
  {
    auto lock = std::scoped_lock{state.mtx};
    progressFilePath = state.progressFilePath;
  }
  if (!progressFilePath.has_value()) { return std::nullopt; }

  auto const progressData = tryReadProgressData(progressFilePath.value());
  if (!progressData.has_value()) { return std::nullopt; }

  {
    auto lock = std::scoped_lock{state.mtx};
    state.lastFrameCount = progressData->frameCount;
  }

  return (static_cast<float>(progressData->frameCount) / state.totalFrames.value())
    * 100.0f;
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

auto startEncodingMonitor(EncodingExecutionContext& executionCtx) -> std::jthread {
  return std::jthread([&] {
    using namespace std::chrono_literals;

    while (true) {
      noteStopRequest(executionCtx.app);
      auto const activeStates = executionCtx.activeStates();

      if (executionCtx.finished() >= executionCtx.overallTotal()) { break; }

      if (stopsignal::isStopRequested() && activeStates.empty()) {
        spdlog::info(
          "Encoding monitor exiting after stop request; no active tasks remain."
        );
        break;
      }

      for (auto const& activeState: activeStates) {
        if (!activeState) { continue; }

        auto const progress = getEncodingProgress(executionCtx.app, *activeState);
        if (!progress.has_value()) {
          auto barIndex = std::optional<std::size_t>{};
          auto lastError = std::optional<std::string>{};
          auto lastStatus = std::optional<std::string>{};
          auto actionId = std::optional<std::string>{};
          {
            auto lock = std::scoped_lock{activeState->mtx};
            barIndex = activeState->barIndex;
            lastError = activeState->lastError;
            lastStatus = activeState->lastStatus;
            actionId = activeState->actionId;
          }

          if (barIndex.has_value()) {
            auto const fileLabel = getStateLabel(*activeState);
            if (lastError.has_value()) {
              executionCtx.progress().setTone(barIndex.value(), progress::Tone::Failure);
              executionCtx.progress().setPostfixText(
                barIndex.value(),
                std::format("Encoding: {} | {}", fileLabel, lastError.value())
              );
            } else if (lastStatus.has_value()) {
              executionCtx.progress().setTone(barIndex.value(), progress::Tone::Active);
              executionCtx.progress().setPostfixText(
                barIndex.value(),
                std::format("Encoding: {} | {}", fileLabel, lastStatus.value())
              );
            }
          }

          if (lastStatus.has_value()) {
            withActionJobState(
              executionCtx.app,
              actionId,
              [&](jobstate::Store& currentStore, std::string const& currentActionId) {
                currentStore.markProgress(
                  currentActionId,
                  std::nullopt,
                  std::nullopt,
                  lastStatus.value()
                );
              }
            );
          }

          continue;
        }

        auto barIndex = std::optional<std::size_t>{};
        auto actionId = std::optional<std::string>{};
        auto lastFrameCount = std::optional<std::uint64_t>{};
        {
          auto lock = std::scoped_lock{activeState->mtx};
          activeState->lastProgress = progress.value();
          activeState->lastProgressAtomic.store(
            progress.value(),
            std::memory_order_release
          );
          barIndex = activeState->barIndex;
          actionId = activeState->actionId;
          lastFrameCount = activeState->lastFrameCount;
        }

        if (barIndex.has_value()) {
          executionCtx.progress().setTone(barIndex.value(), progress::Tone::Active);
          executionCtx.progress().setProgress(barIndex.value(), progress.value());
        }

        withActionJobState(
          executionCtx.app,
          actionId,
          [&](jobstate::Store& currentStore, std::string const& currentActionId) {
            currentStore.markProgress(
              currentActionId,
              progress.value(),
              lastFrameCount,
              std::nullopt
            );
          }
        );
      }

      executionCtx.updateOverall();

      std::this_thread::sleep_for(20ms);
    }

    executionCtx.updateOverall();
  });
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
      executionCtx.barEncodingStatus(*vidState, fileLabel, status);
      auto actionId = std::optional<std::string>{};
      auto lock = std::scoped_lock{vidState->mtx};
      vidState->lastStatus = status;
      actionId = vidState->actionId;
      withActionJobState(
        executionCtx.app,
        actionId,
        [&](jobstate::Store& store, std::string const& currentActionId) {
          store.markProgress(currentActionId, std::nullopt, std::nullopt, status);
        }
      );
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
    withActionJobState(
      ctx,
      state.actionId,
      [](jobstate::Store& store, std::string const& currentActionId) {
        store.markRunning(currentActionId);
      }
    );

    auto const success = encodeVideo(ctx, state, {});
    if (state.progressFilePath.has_value()) {
      auto ec = std::error_code{};
      fs::remove(state.progressFilePath.value(), ec);
    }
    vidsRunRes = vidsRunRes.set(vidPath, success);
    withActionJobState(
      ctx,
      state.actionId,
      [&](jobstate::Store& store, std::string const& currentActionId) {
        if (success) {
          store.markSucceeded(currentActionId);
        } else {
          store.markFailed(currentActionId, state.lastError.value_or("encoding failed"));
        }
      }
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
      "do you want to encode the video to {} format? (y/N): ",
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
  auto monitorThread = startEncodingMonitor(executionCtx);

  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(vids.size());
  for (auto taskIndex = std::size_t{0}; taskIndex < vids.size(); ++taskIndex) {
    tasks.push_back(
      taskexec::TaskSpec{
        .id = std::format("encode:{}", vids[taskIndex].string()),
        .label = vids[taskIndex].filename().string(),
        .run = [&, taskIndex, vidPath = vids[taskIndex]](taskexec::TaskContext& taskCtx)
          -> eh::Result<void> {
          return runEncodingTask(executionCtx, taskIndex, vidPath, taskCtx.slot);
        }
      }
    );
  }

  auto const runState = taskexec::runTasks(
    taskexec::TaskPlan{
      .tasks = std::move(tasks),
      .maxConcurrency = maxConcurrentJobs,
      .progress = &progressState.progressCtx,
      .hideCursor = true,
    }
  );

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
