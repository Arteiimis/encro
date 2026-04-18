#include "pack/pack_service.h"

#include "core/parallel.h"
#include "core/progress.h"
#include "core/stop_signal.h"
#include "pack/packer.h"

#include <algorithm>
#include <atomic>
#include <format>

namespace pack {

auto buildGroupOrdinalRanges(std::vector<std::vector<fs::path>> const& groups)
  -> std::vector<FileOrdinalRange> {
  auto ranges = std::vector<FileOrdinalRange>{};
  ranges.reserve(groups.size());

  auto nextOrdinal = std::size_t{1};
  for (auto const& group: groups) {
    if (group.empty()) {
      ranges.push_back({});
      continue;
    }

    auto const first = nextOrdinal;
    auto const last = first + group.size() - 1;
    ranges.push_back({first, last, group.size()});
    nextOrdinal = last + 1;
  }

  return ranges;
}

auto appendOrdinalRangeSuffix(
  std::string_view fileName,
  FileOrdinalRange const& range
) -> std::string {
  if (range.first == 0 || range.last == 0 || range.count == 0) {
    return std::string{fileName};
  }

  auto const filePath = fs::path{fileName};
  auto const suffix = std::format("[{}~{}#{}p]", range.first, range.last, range.count);
  return std::format(
    "{}{}{}",
    filePath.stem().string(),
    suffix,
    filePath.extension().string()
  );
}

auto packGroupsParallel(PackPlan const& plan) -> eh::Result<std::vector<fs::path>> {
  if (plan.groups.empty()) { return std::vector<fs::path>{}; }

  fs::create_directories(plan.outputDir);

  auto const maxParallelJobs =
    std::max<std::size_t>(1, plan.maxParallelJobs.value_or(plan.groups.size()));
  auto const workerCount = std::min(plan.groups.size(), maxParallelJobs);

  auto progressCtx = progress::ProgressContext{};
  auto packResults = std::vector<eh::Result<void>>(plan.groups.size());
  auto zippedFiles = std::vector<fs::path>(plan.groups.size());
  auto attempted = std::vector<bool>(plan.groups.size(), false);
  auto nextIndex = std::atomic<std::size_t>{0};
  auto processed = std::atomic<std::size_t>{0};

  auto _ = progress::CursorGuard{};
  parallel::runIndexedTasks(workerCount, workerCount, [&](std::size_t) {
    while (true) {
      if (stopsignal::isStopRequested()) { break; }

      auto const index = nextIndex.fetch_add(1);
      if (index >= plan.groups.size()) { break; }

      attempted[index] = true;
      if (plan.onGroupStart) { plan.onGroupStart(index); }

      auto const zipName = plan.zipNameForIndex ? plan.zipNameForIndex(index)
                                                : std::format("part{}.zip", index + 1);
      auto const zipPath = plan.outputDir / zipName;
      auto const label = plan.progressLabelForIndex
        ? plan.progressLabelForIndex(index)
        : std::format("Packing: {}", zipName);

      auto const packRes = packFilesToZip(
        plan.groups[index],
        zipPath,
        progressCtx,
        label,
        plan.zipEntryNameForFile
      );

      if (!packRes) {
        if (plan.removeOnFailure) {
          auto ec = std::error_code{};
          fs::remove(zipPath, ec);
        }
        packResults[index] = packRes;
        if (plan.onGroupFailure) {
          plan.onGroupFailure(index, packRes.error());
        }
        processed.fetch_add(1, std::memory_order_release);
        continue;
      }

      packResults[index] = {};
      zippedFiles[index] = zipPath;
      if (plan.onGroupSuccess) { plan.onGroupSuccess(index, zipPath); }
      processed.fetch_add(1, std::memory_order_release);
    }
  });

  if (
    stopsignal::isStopRequested()
    && processed.load(std::memory_order_acquire) < plan.groups.size()
  ) {
    return eh::makeError("Packing canceled by user.");
  }

  for (auto index = std::size_t{0}; index < packResults.size(); ++index) {
    if (!attempted[index]) { continue; }
    if (!packResults[index]) {
      return eh::makeError("{}", packResults[index].error());
    }
  }

  return zippedFiles;
}

}  // namespace pack
