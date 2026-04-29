#pragma once

#include "pack/pack_service.h"
#include "pack/packer.h"

#include <atomic>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace pack_facade {

// === Utility functions ===
[[deprecated("Use PackService::buildGroupOrdinalRanges")]]
inline auto
buildGroupOrdinalRanges(std::vector<std::vector<std::filesystem::path>> const& groups)
  -> std::vector<pack::FileOrdinalRange> {
  return pack::PackService::buildGroupOrdinalRanges(groups);
}

[[deprecated("Use PackService::buildGroupOrdinalRanges")]]
inline auto
buildGroupOrdinalRanges(std::vector<std::vector<pack::PackFileEntry>> const& groups)
  -> std::vector<pack::FileOrdinalRange> {
  return pack::PackService::buildGroupOrdinalRanges(groups);
}

[[deprecated("Use PackService::appendOrdinalRangeSuffix")]]
inline auto
appendOrdinalRangeSuffix(std::string_view fileName, pack::FileOrdinalRange const& range)
  -> std::string {
  return pack::PackService::appendOrdinalRangeSuffix(fileName, range);
}

[[deprecated("Use PackService::defaultZipNameForIndex")]]
inline auto defaultZipNameForIndex(std::size_t index) -> std::string {
  return pack::PackService::defaultZipNameForIndex(index);
}

[[deprecated("Use PackService::defaultProgressLabelForZipName")]]
inline auto defaultProgressLabelForZipName(std::string_view zipName) -> std::string {
  return pack::PackService::defaultProgressLabelForZipName(zipName);
}

[[deprecated("Use PackService::resolveZipNameForIndex")]]
inline auto resolveZipNameForIndex(pack::PackPlan const& plan, std::size_t index)
  -> std::string {
  return pack::PackService::resolveZipNameForIndex(plan, index);
}

[[deprecated("Use PackService::resolveProgressLabelForIndex")]]
inline auto resolveProgressLabelForIndex(pack::PackPlan const& plan, std::size_t index)
  -> std::string {
  return pack::PackService::resolveProgressLabelForIndex(plan, index);
}

// === Plan orchestration ===
[[deprecated("Use PackService::selectPackPlanIndexes")]]
inline auto
selectPackPlanIndexes(pack::PackPlan const& plan, std::span<std::size_t const> indexes)
  -> pack::PackPlan {
  return pack::PackService::selectPackPlanIndexes(plan, indexes);
}

[[deprecated("Use PackService::runPackPlan")]]
inline auto runPackPlan(appctx::AppContext& ctx, pack::PackPlan const& plan)
  -> eh::Result<pack::PackRunResult> {
  static pack::Packer packer;
  static pack::PackService service(packer);
  return service.runPackPlan(ctx, plan);
}

[[deprecated("Use PackService::packGroups")]]
inline auto packGroups(pack::PackPlan const& plan)
  -> eh::Result<std::vector<std::filesystem::path>> {
  static pack::Packer packer;
  static pack::PackService service(packer);
  return service.packGroups(plan);
}

// === Workflow methods ===
[[deprecated("Use PackService::packAllFilesInDirectory")]]
inline auto packAllFilesInDirectory(
  std::filesystem::path const& dirPath,
  std::filesystem::path const& zipFileDir,
  std::uintmax_t maxGroupSize = pack::kDefaultMaxArchiveGroupSize,
  bool recursive = true,
  bool forceNameConflictHandling = false,
  std::optional<std::size_t> maxParallelJobs = std::nullopt
) -> eh::Result<void> {
  static pack::Packer packer;
  static pack::PackService service(packer);
  return service.packAllFilesInDirectory(
    dirPath,
    zipFileDir,
    maxGroupSize,
    recursive,
    forceNameConflictHandling,
    maxParallelJobs
  );
}

[[deprecated("Use PackService::runDirectoryPackWorkflow")]]
inline auto
runDirectoryPackWorkflow(appctx::AppContext& ctx, std::filesystem::path const& dirPath)
  -> eh::Result<int> {
  static pack::Packer packer;
  static pack::PackService service(packer);
  return service.runDirectoryPackWorkflow(ctx, dirPath);
}

// === Packer methods ===
[[deprecated("Use Packer::packFilesToZip")]]
inline auto packFilesToZip(
  std::vector<std::filesystem::path> const& filePaths,
  std::filesystem::path const& zipFilePath,
  progress::ProgressContext& progressCtx,
  std::string_view progressText,
  pack::detail::ZipEntryNameResolver entryNameForFile = {}
) -> eh::Result<void> {
  static pack::Packer packer;
  static pack::PackService service(packer);
  return packer.packFilesToZip(
    filePaths,
    zipFilePath,
    progressCtx,
    progressText,
    std::move(entryNameForFile)
  );
}

[[deprecated("Use Packer::packFilesToZip")]]
inline auto packFilesToZip(
  std::vector<pack::PackFileEntry> const& entries,
  std::filesystem::path const& zipFilePath,
  progress::ProgressContext& progressCtx,
  std::string_view progressText
) -> eh::Result<void> {
  static pack::Packer packer;
  static pack::PackService service(packer);
  return packer.packFilesToZip(entries, zipFilePath, progressCtx, progressText);
}

[[deprecated("Use Packer::packFilesToZip")]]
inline auto packFilesToZip(
  std::vector<pack::PackFileEntry> const& entries,
  std::filesystem::path const& zipFilePath,
  pack::detail::PackEntryProgressCallback onEntryPacked = {},
  std::atomic<std::size_t>* finalizingCount = nullptr
) -> eh::Result<void> {
  static pack::Packer packer;
  static pack::PackService service(packer);
  return packer.packFilesToZip(entries, zipFilePath, onEntryPacked, finalizingCount);
}

// === Grouping functions ===
[[deprecated("Use Packer::groupFilesBySize")]]
inline auto groupFilesBySize(
  std::vector<std::filesystem::path> const& filePaths,
  std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
  std::optional<std::size_t> maxFilesPerGroup = std::nullopt
) -> std::vector<std::vector<std::filesystem::path>> {
  static pack::Packer packer;
  static pack::PackService service(packer);
  return packer.groupFilesBySize(filePaths, maxGroupSize, maxFilesPerGroup);
}

[[deprecated("Use Packer::groupPackFiles")]]
inline auto groupPackFiles(
  std::vector<pack::detail::PackGroupInput> const& filePaths,
  std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
  std::optional<std::size_t> maxFilesPerGroup = std::nullopt,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
) -> std::vector<std::vector<std::filesystem::path>> {
  static pack::Packer packer;
  static pack::PackService service(packer);
  return packer.groupPackFiles(
    filePaths,
    maxGroupSize,
    maxFilesPerGroup,
    keepSourceDirsTogetherWhenTotalFilesExceed
  );
}

[[deprecated("Use Packer::groupPackFilesWithSubparts")]]
inline auto groupPackFilesWithSubparts(
  std::vector<pack::detail::PackGroupInput> const& filePaths,
  std::uintmax_t maxGroupSize,
  std::size_t maxFilesPerPart,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
) -> std::vector<pack::detail::PackGroupPartition> {
  static pack::Packer packer;
  static pack::PackService service(packer);
  return packer.groupPackFilesWithSubparts(
    filePaths,
    maxGroupSize,
    maxFilesPerPart,
    keepSourceDirsTogetherWhenTotalFilesExceed
  );
}

[[deprecated("Use Packer::groupPackEntries")]]
inline auto groupPackEntries(
  std::vector<pack::detail::PackEntryInput> const& entries,
  std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
  std::optional<std::size_t> maxFilesPerGroup = std::nullopt,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
) -> std::vector<std::vector<pack::PackFileEntry>> {
  static pack::Packer packer;
  static pack::PackService service(packer);
  return packer.groupPackEntries(
    entries,
    maxGroupSize,
    maxFilesPerGroup,
    keepSourceDirsTogetherWhenTotalFilesExceed
  );
}

[[deprecated("Use Packer::groupPackEntriesWithSubparts")]]
inline auto groupPackEntriesWithSubparts(
  std::vector<pack::detail::PackEntryInput> const& entries,
  std::uintmax_t maxGroupSize,
  std::size_t maxFilesPerPart,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
) -> std::vector<pack::detail::PackEntryPartition> {
  static pack::Packer packer;
  static pack::PackService service(packer);
  return packer.groupPackEntriesWithSubparts(
    entries,
    maxGroupSize,
    maxFilesPerPart,
    keepSourceDirsTogetherWhenTotalFilesExceed
  );
}

[[deprecated("Use Packer::buildDirectoryPackPlan")]]
inline auto buildDirectoryPackPlan(
  std::filesystem::path const& dirPath,
  std::filesystem::path const& zipFileDir,
  std::uintmax_t maxGroupSize = pack::kDefaultMaxArchiveGroupSize,
  bool recursive = true,
  bool forceNameConflictHandling = false,
  std::optional<std::size_t> maxParallelJobs = std::nullopt,
  std::optional<std::filesystem::path> excludedPath = std::nullopt
) -> eh::Result<pack::PackPlan> {
  static pack::Packer packer;
  static pack::PackService service(packer);
  return packer.buildDirectoryPackPlan(
    dirPath,
    zipFileDir,
    maxGroupSize,
    recursive,
    forceNameConflictHandling,
    maxParallelJobs,
    excludedPath
  );
}

}  // namespace pack_facade
