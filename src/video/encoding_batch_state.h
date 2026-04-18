#pragma once

#include "core/app_context.h"
#include "core/progress.h"

#include <atomic>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

struct EncodingBatchState {
  struct Counters {
    std::atomic_size_t finished;
    std::atomic_size_t nextTask;
    std::size_t pendingTotal;
    std::size_t overallTotal;
    std::size_t workers;
    std::optional<std::size_t> overallBarIndex;
  } counters;

  struct Slots {
    appctx::EncodingStateList active;
    std::mutex activeMtx;
    std::vector<std::size_t> barIndexes;
  } slots;

  struct Results {
    std::unordered_map<fs::path, bool> map;
    std::mutex mtx;
  } results;

  progress::ProgressContext progressCtx;

  EncodingBatchState(std::size_t total, std::size_t workers)
    : EncodingBatchState(total, total, 0, workers) { }

  EncodingBatchState(
    std::size_t pendingTotal,
    std::size_t overallTotal,
    std::size_t completedBeforeStart,
    std::size_t workers
  )
    : counters{
        std::atomic_size_t{std::min(completedBeforeStart, overallTotal)},
        std::atomic_size_t{0},
        pendingTotal,
        overallTotal,
        workers,
        std::nullopt
      },
      slots{
        appctx::EncodingStateList(workers),
        std::mutex{},
        std::vector<std::size_t>{},
      },
      results{},
      progressCtx{} {
    counters.overallBarIndex = createOverallBar(
      progressCtx,
      overallTotal,
      completedBeforeStart,
      workers
    );
    slots.barIndexes = makeSlotBars(progressCtx, workers);
  }

private:
  static std::optional<std::size_t> createOverallBar(
    progress::ProgressContext& progressCtx,
    std::size_t totalTasks,
    std::size_t completedBeforeStart,
    std::size_t workerCount
  ) {
    if (totalTasks <= workerCount) { return std::nullopt; }
    return progressCtx.addBar(
      std::format(
        "Overall: {}/{}",
        std::min(completedBeforeStart, totalTasks),
        totalTasks
      )
    );
  }

  static std::vector<std::size_t>
  makeSlotBars(progress::ProgressContext& progressCtx, std::size_t workerCount) {
    auto barIndexes = std::vector<std::size_t>(workerCount);
    for (auto slot = std::size_t{0}; slot < workerCount; ++slot) {
      barIndexes[slot] =
        progressCtx.addBar(std::format("Encoding: [idle-{}]", slot + 1));
    }
    return barIndexes;
  }
};
