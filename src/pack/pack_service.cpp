#include "pack/pack_service.h"

#include "core/task_executor.h"
#include "pack/packer.h"

#include <algorithm>
#include <format>
#include <memory>

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

auto buildGroupOrdinalRanges(std::vector<std::vector<PackFileEntry>> const& groups)
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

auto appendOrdinalRangeSuffix(std::string_view fileName, FileOrdinalRange const& range)
  -> std::string {
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

auto defaultZipNameForIndex(std::size_t index) -> std::string {
  return std::format("part{}.zip", index + 1);
}

auto defaultProgressLabelForZipName(std::string_view zipName) -> std::string {
  return std::format("Packing: {}", zipName);
}

auto resolveZipNameForIndex(PackPlan const& plan, std::size_t index) -> std::string {
  return plan.zipNameForIndex ? plan.zipNameForIndex(index)
                              : defaultZipNameForIndex(index);
}

auto resolveProgressLabelForIndex(PackPlan const& plan, std::size_t index)
  -> std::string {
  if (plan.progressLabelForIndex) { return plan.progressLabelForIndex(index); }

  return defaultProgressLabelForZipName(resolveZipNameForIndex(plan, index));
}

auto selectPackPlanIndexes(PackPlan const& plan, std::span<std::size_t const> indexes)
  -> PackPlan {
  auto filteredGroups = std::vector<std::vector<PackFileEntry>>{};
  filteredGroups.reserve(indexes.size());
  for (auto const index: indexes) { filteredGroups.push_back(plan.groups[index]); }

  auto const selectedIndexes =
    std::make_shared<std::vector<std::size_t>>(indexes.begin(), indexes.end());

  return PackPlan{
    .groups = std::move(filteredGroups),
    .outputDir = plan.outputDir,
    .zipNameForIndex =
      [base = plan.zipNameForIndex, selectedIndexes](std::size_t subsetIndex) {
        auto const actualIndex = selectedIndexes->at(subsetIndex);
        return base ? base(actualIndex) : defaultZipNameForIndex(actualIndex);
      },
    .progressLabelForIndex =  //
    plan.progressLabelForIndex
      ? std::function<std::string(std::size_t)>{ //
        [base = plan.progressLabelForIndex, selectedIndexes](std::size_t subsetIndex) {
          return base(selectedIndexes->at(subsetIndex));
        }
      }
      : std::function<std::string(std::size_t)>{},
    .onGroupStart = {},
    .onGroupSuccess = {},
    .onGroupFailure = {},
    .maxParallelJobs = plan.maxParallelJobs,
    .removeOnFailure = plan.removeOnFailure,
  };
}

auto packGroups(PackPlan const& plan) -> eh::Result<std::vector<fs::path>> {
  if (plan.groups.empty()) { return std::vector<fs::path>{}; }

  fs::create_directories(plan.outputDir);

  auto const maxParallelJobs =
    std::max<std::size_t>(1, plan.maxParallelJobs.value_or(plan.groups.size()));
  auto packResults = std::vector<eh::Result<void>>(plan.groups.size());
  auto zippedFiles = std::vector<fs::path>(plan.groups.size());
  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(plan.groups.size());

  for (auto index = std::size_t{0}; index < plan.groups.size(); ++index) {
    auto const zipName = resolveZipNameForIndex(plan, index);
    auto const zipPath = plan.outputDir / zipName;
    auto const label = resolveProgressLabelForIndex(plan, index);

    tasks.push_back(
      taskexec::TaskSpec{
        .id = std::format("pack:{}", index),
        .label = label,
        .run =
          [&, index, zipPath, label](taskexec::TaskContext& taskCtx) -> eh::Result<void> {
          if (plan.onGroupStart) { plan.onGroupStart(index); }

          auto const packRes =
            packFilesToZip(plan.groups[index], zipPath, taskCtx.progress, label);

          if (!packRes) {
            if (plan.removeOnFailure) {
              auto ec = std::error_code{};
              fs::remove(zipPath, ec);
            }
            packResults[index] = packRes;
            if (plan.onGroupFailure) { plan.onGroupFailure(index, packRes.error()); }
            return eh::makeError("{}", packRes.error());
          }

          packResults[index] = {};
          zippedFiles[index] = zipPath;
          if (plan.onGroupSuccess) { plan.onGroupSuccess(index, zipPath); }
          return {};
        }
      }
    );
  }

  auto const runRes = taskexec::runTasks(
    taskexec::TaskPlan{
      .tasks = std::move(tasks),
      .maxConcurrency = maxParallelJobs,
      .progress = nullptr,
      .hideCursor = true,
    }
  );

  if (runRes.canceled && runRes.attemptedCount < plan.groups.size()) {
    return eh::makeError("Packing canceled by user.");
  }

  for (auto index = std::size_t{0}; index < packResults.size(); ++index) {
    if (runRes.attempted[index] == 0) { continue; }
    if (!packResults[index]) { return eh::makeError("{}", packResults[index].error()); }
  }

  return zippedFiles;
}

}  // namespace pack
