#pragma once

#include "core/app_context.h"
#include "core/progress.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace videobatch {

// Lookup-only action-id map keyed by input path. Unordered by design: only
// ever queried via find(), never iterated for user-visible output.
using ActionIdMap = appctx::path_map<std::string>;
// Ordered result map (path-sorted iteration) — the failure list and zip
// member order in the summary/output collection depend on this ordering.
using EncodeResultsMap = std::map<fs::path, bool>;

struct EncodingBatchOutcome {
  std::optional<EncodeResultsMap> results;     // nullopt = canceled at the prompt
  std::vector<std::string> attentionWarnings;  // unreachable-floor files
  bool dryRun = false;  // probe plan printed; exit without encoding
};

// Batch job description threaded through the encode pipeline: the files to
// encode, where each output lands, and the job-state action ids tracking
// them. Produced together by the caller and consumed as one unit.
struct EncodingBatchJob {
  std::vector<fs::path> vids;
  appctx::path_map<fs::path> plannedOutputFiles;
  ActionIdMap actionIds;
};

auto runEncodingTasks(
  appctx::AppContext& ctx,
  EncodingBatchJob const& job,
  std::size_t overallTotalCount,
  std::size_t initialCompletedCount
) -> EncodingBatchOutcome;

namespace detail {

struct EncodingProgressState {
  using ActiveSlots = std::vector<appctx::EncodingStatePtr>;

  struct Counters {
    std::atomic_size_t finished;
    std::size_t pendingTotal;
    std::size_t overallTotal;
    std::size_t workers;
    std::optional<std::size_t> overallBarIndex;
  } counters;

  std::mutex slotsMtx;
  ActiveSlots activeSlots;

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
      slotsMtx{},
      activeSlots(workers),
      slots{
        std::vector<std::size_t>{},
      },
      progressCtx{} {
    counters.overallBarIndex =
      createOverallBar(progressCtx, overallTotal, completedBeforeStart, workers, compact);
    slots.barIndexes = makeSlotBars(progressCtx, workers, compact, overallTotal);
  }

private:
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

  static std::vector<std::size_t> makeSlotBars(
    progress::ProgressContext& progressCtx,
    std::size_t workerCount,
    bool compact,
    std::size_t totalTasks
  ) {
    if (compact && totalTasks > 1) { return {}; }
    auto barIndexes = std::vector<std::size_t>(workerCount);
    for (auto slot = std::size_t{0}; slot < workerCount; ++slot) {
      barIndexes[slot] =
        progressCtx
          .addBar(std::format("Encoding: [idle-{}]", slot + 1), progress::Tone::Idle);
    }
    return barIndexes;
  }
};

struct EncodingExecutionContext {
  appctx::AppContext& app;
  EncodingProgressState& progressState;
  appctx::path_map<fs::path> const& plannedOutputFiles;
  videobatch::ActionIdMap const& actionIds;
  // Per-file probe decisions (input path -> chosen CQ), copied when execution
  // contexts are created after the confirmation gate; empty when probing was
  // skipped (--crf, webp, or short videos).
  appctx::path_map<int> probeCqByInput;

  auto& counters() { return progressState.counters; }
  auto const& counters() const { return progressState.counters; }
  auto& slots() { return progressState.slots; }
  auto const& slots() const { return progressState.slots; }
  auto& progress() { return progressState.progressCtx; }
  auto const& progress() const { return progressState.progressCtx; }

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
    auto lock = std::scoped_lock{progressState.slotsMtx};
    progressState.activeSlots[slot] = vidState;
  }

  void clearActive(std::size_t slot) {
    auto lock = std::scoped_lock{progressState.slotsMtx};
    progressState.activeSlots[slot] = nullptr;
  }

  auto activeState(std::size_t slot) const -> appctx::EncodingStatePtr {
    auto lock = std::scoped_lock{progressState.slotsMtx};
    return progressState.activeSlots[slot];
  }

  auto activeStates() -> appctx::EncodingStateList {
    auto activeStates = appctx::EncodingStateList{};
    {
      auto lock = std::scoped_lock{progressState.slotsMtx};
      activeStates.reserve(progressState.activeSlots.size());
      for (auto const& activeState: progressState.activeSlots) {
        if (activeState) { activeStates.push_back(activeState); }
      }
    }
    return activeStates;
  }

  void barEncodingStart(appctx::EncodingState& vidState, std::string_view fileLabel) {
    if (!vidState.barIndex.has_value()) { return; }
    auto const index = vidState.barIndex.value();
    progress().setTone(index, progress::Tone::Active);
    progress().resetEta(index);
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
    progress()
      .setPostfixText(barIndex.value(), std::format("Encoding: [idle-{}]", slot + 1));
  }

  void barDone(
    std::optional<std::size_t> barIndex,
    bool success,
    std::string_view fileLabel
  ) {
    if (!barIndex.has_value()) { return; }
    progress().setTone(
      barIndex.value(),
      success ? progress::Tone::Success : progress::Tone::Failure
    );
    if (success) { progress().setProgress(barIndex.value(), 100.0f); }
    progress().setPostfixText(
      barIndex.value(),
      std::format("{}: {}", success ? "Done" : "Failed", fileLabel)
    );
  }

  void updateOverall() {
    auto const overallBarIndex = counters().overallBarIndex;
    if (!overallBarIndex.has_value()) { return; }

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
    auto const totalCount = static_cast<float>(
      overallTotal()
    );  // NOLINT(bugprone-narrowing-conversions): progress percent needs float; size_t precision loss irrelevant
    auto overallPercent = 0.0f;
    if (totalCount > 0.0f) {
      overallPercent = std::min(
        100.0f,
        // NOLINTNEXTLINE(bugprone-narrowing-conversions): completed is size_t; float progress math is fine
        (completed + activeProgress) / totalCount * 100.0f
      );
    }

    progress().setProgress(overallBarIndex.value(), overallPercent);
    progress().setPostfixText(
      overallBarIndex.value(),
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
      vidState->lastProgressAtomic.store(100.0f, std::memory_order_release);

      progressFileToRemove = vidState->progressFilePath;
    }

    if (progressFileToRemove.has_value()) {
      auto ec = std::error_code{};
      fs::remove(progressFileToRemove.value(), ec);
    }
  }
};

auto startEncodingMonitor(EncodingExecutionContext& executionCtx) -> std::jthread;

}  // namespace detail

}  // namespace videobatch
