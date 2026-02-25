#pragma once

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
    std::size_t total;
    std::size_t workers;
    std::optional<std::size_t> overallBarIndex;
  } counters;

  struct Slots {
    std::vector<std::optional<fs::path>> taskPaths;
    std::mutex taskPathsMtx;
    std::vector<std::size_t> barIndexes;
    std::vector<float> progress;
    std::mutex progressMtx;
  } slots;

  struct Results {
    std::unordered_map<fs::path, bool> map;
    std::mutex mtx;
  } results;

  progress::ProgressContext progressCtx;

  EncodingBatchState(std::size_t total, std::size_t workers)
    : counters{std::atomic_size_t{0}, std::atomic_size_t{0}, total, workers, std::nullopt},
      slots{
        std::vector<std::optional<fs::path>>(workers),
        std::mutex{},
        std::vector<std::size_t>{},
        std::vector<float>(workers, 0.0f),
        std::mutex{}
      },
      results{},
      progressCtx{} {
    counters.overallBarIndex = createOverallBar(progressCtx, total, workers);
    slots.barIndexes = makeSlotBars(progressCtx, workers);
  }

private:
  static std::optional<std::size_t> createOverallBar(
    progress::ProgressContext& progressCtx,
    std::size_t totalTasks,
    std::size_t workerCount
  ) {
    if (totalTasks <= workerCount) { return std::nullopt; }
    return progressCtx.addBar(std::format("Overall: 0/{}", totalTasks));
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
