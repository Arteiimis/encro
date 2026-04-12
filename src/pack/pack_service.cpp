#include "pack/pack_service.h"

#include "core/parallel.h"
#include "core/progress.h"
#include "pack/packer.h"

#include <algorithm>
#include <format>
#include <mutex>

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
  auto const suffix =
    std::format("_{}-{}_items{}", range.first, range.last, range.count);
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
  auto resultMtx = std::mutex{};

  auto _ = progress::CursorGuard{};
  parallel::runIndexedTasks(plan.groups.size(), workerCount, [&](std::size_t index) {
    auto const zipName = plan.zipNameForIndex ? plan.zipNameForIndex(index)
                                              : std::format("part{}.zip", index + 1);
    auto const zipPath = plan.outputDir / zipName;
    auto const label = plan.progressLabelForIndex
                       ? plan.progressLabelForIndex(index)
                       : std::format("Packing: {}", zipName);

    auto const packRes =
      packFilesToZip(
        plan.groups[index],
        zipPath,
        progressCtx,
        label,
        plan.zipEntryNameForFile
      );

    auto lock = std::scoped_lock{resultMtx};
    if (!packRes) {
      if (plan.removeOnFailure) {
        auto ec = std::error_code{};
        fs::remove(zipPath, ec);
      }
      packResults[index] = packRes;
      return;
    }

    packResults[index] = {};
    zippedFiles[index] = zipPath;
  });

  for (auto const& res: packResults) {
    if (!res) { return eh::makeError("{}", res.error()); }
  }

  return zippedFiles;
}

}  // namespace pack
