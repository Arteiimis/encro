#pragma once

#include "core/app_context.h"
#include "core/encoding_state.h"
#include "core/progress.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace videobatch {

// What the encode phase reports that is not per-item: the plan, the job-state
// action id, the probe decision and each file's outcome all live on the item.
struct EncodingBatchSummary {
  // Probe aborted or failed, or the confirmation was declined: the caller
  // returns the cancel exit code and prints no summary.
  bool canceled = false;
  std::vector<std::string> attentionWarnings;  // unreachable-floor files
  bool dryRun = false;  // probe plan printed; exit without encoding
  // Files dropped before encoding (estimated output larger than the source);
  // they never reach the encode stage, so the summary needs them to report a
  // total that the outcome classes add up to.
  std::size_t skippedCount = 0;
  // Wall time the encode batch itself took, excluding the probe stage and the
  // confirmation prompt.
  std::chrono::milliseconds encodeElapsed{};
};

// Runs the encode phase over the pending items, writing each file's outcome
// back onto its item.
auto runEncodingTasks(
  appctx::AppContext& ctx,
  std::span<appctx::EncodingStatePtr const> items,
  std::size_t overallTotalCount,
  std::size_t initialCompletedCount
) -> EncodingBatchSummary;

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
    bool const showOverall = progress::showsOverallBar(totalTasks, workerCount, compact);
    if (!showOverall) { return std::optional<std::size_t>{}; }
    return std::optional<std::size_t>{progressCtx.addBar(
      std::format(
        "Overall: {}/{}",
        std::min(completedBeforeStart, totalTasks),
        totalTasks
      ),
      terminal::Role::Accent
    )};
  }

  static std::vector<std::size_t> makeSlotBars(
    progress::ProgressContext& progressCtx,
    std::size_t workerCount,
    bool compact,
    std::size_t totalTasks
  ) {
    if (!progress::showsSlotBars(totalTasks, compact)) { return {}; }
    auto barIndexes = std::vector<std::size_t>(workerCount);
    for (auto slot = std::size_t{0}; slot < workerCount; ++slot) {
      barIndexes[slot] =
        progressCtx
          .addBar(std::format("Encoding: [idle-{}]", slot + 1), terminal::Role::Accent);
    }
    return barIndexes;
  }
};

struct EncodingExecutionContext {
  appctx::AppContext& app;
  EncodingProgressState& progressState;

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

  // The Overall bar's text. One formatter for the bar and for the runner's
  // completion hook, so the count the hook writes cannot drift from the count
  // the monitor writes.
  auto overallText() const -> std::string {
    return std::format("Overall: {}/{}", finished(), overallTotal());
  }

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

  void barEncodingStart(
    appctx::EncodingState& vidState,
    std::string_view fileLabel,
    std::chrono::milliseconds elapsedBase = std::chrono::milliseconds{0}
  ) {
    if (!vidState.barIndex.has_value()) { return; }
    auto const index = vidState.barIndex.value();
    progress().setRole(index, terminal::Role::Accent);
    progress().resetEta(
      index,
      std::chrono::duration_cast<std::chrono::duration<float>>(elapsedBase).count()
    );
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
    progress().setRole(index, terminal::Role::Accent);
    progress().setPostfixText(index, std::format("Encoding: {} | {}", fileLabel, status));
  }

  void barIdle(std::optional<std::size_t> barIndex, std::size_t slot) {
    if (!barIndex.has_value()) { return; }
    progress().setRole(barIndex.value(), terminal::Role::Accent);
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
    progress()
      .setRole(barIndex.value(), success ? terminal::Role::Good : terminal::Role::Bad);
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
    progress().setPostfixText(overallBarIndex.value(), overallText());
  }
};

auto startEncodingMonitor(EncodingExecutionContext& executionCtx) -> std::jthread;

// Accumulated encoding time persisted for the task; 0 when the action is
// unknown or has never been settled.
auto persistedElapsedMs(
  jobstate::Store& store,
  std::optional<std::string> const& actionId
) -> std::chrono::milliseconds;

}  // namespace detail

}  // namespace videobatch
