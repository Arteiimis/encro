#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"
#include "core/progress.h"
#include "pack/pack_types.h"
#include "pack/packer_types.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pack::detail {

using ZipEntryNameResolver = std::function<std::string(std::filesystem::path const&)>;
using PackEntryProgressCallback = std::function<void(std::size_t, std::size_t)>;

}  // namespace pack::detail

auto packFilesToZip(
  std::vector<std::filesystem::path> const& filePaths,
  std::filesystem::path const& zipFilePath,
  progress::ProgressContext& progressCtx,
  std::string_view progressText,
  pack::detail::ZipEntryNameResolver entryNameForFile = {}
) -> eh::Result<void>;

auto packFilesToZip(
  std::vector<pack::PackFileEntry> const& entries,
  std::filesystem::path const& zipFilePath,
  progress::ProgressContext& progressCtx,
  std::string_view progressText
) -> eh::Result<void>;

auto packFilesToZip(
  std::vector<pack::PackFileEntry> const& entries,
  std::filesystem::path const& zipFilePath,
  pack::detail::PackEntryProgressCallback onEntryPacked = {},
  std::atomic<std::size_t>* finalizingCount = nullptr
) -> eh::Result<void>;

auto groupFilesBySize(
  std::vector<std::filesystem::path> const& filePaths,
  std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
  std::optional<std::size_t> maxFilesPerGroup = std::nullopt
) -> std::vector<std::vector<std::filesystem::path>>;

auto groupPackFiles(
  std::vector<pack::detail::PackGroupInput> const& filePaths,
  std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
  std::optional<std::size_t> maxFilesPerGroup = std::nullopt,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
) -> std::vector<std::vector<std::filesystem::path>>;

auto groupPackFilesWithSubparts(
  std::vector<pack::detail::PackGroupInput> const& filePaths,
  std::uintmax_t maxGroupSize,
  std::size_t maxFilesPerPart,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
) -> std::vector<pack::detail::PackGroupPartition>;

auto groupPackEntries(
  std::vector<pack::detail::PackEntryInput> const& entries,
  std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
  std::optional<std::size_t> maxFilesPerGroup = std::nullopt,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
) -> std::vector<std::vector<pack::PackFileEntry>>;

auto groupPackEntriesWithSubparts(
  std::vector<pack::detail::PackEntryInput> const& entries,
  std::uintmax_t maxGroupSize,
  std::size_t maxFilesPerPart,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
) -> std::vector<pack::detail::PackEntryPartition>;

auto packAllFilesInDirectory(
  std::filesystem::path const& dirPath,
  std::filesystem::path const& zipFileDir,
  std::uintmax_t maxGroupSize = pack::kDefaultMaxArchiveGroupSize,
  bool recursive = true,
  bool forceNameConflictHandling = false,
  std::optional<std::size_t> maxParallelJobs = std::nullopt
) -> eh::Result<void>;

auto runDirectoryPackWorkflow(
  appctx::AppContext& ctx,
  std::filesystem::path const& dirPath
) -> eh::Result<int>;

auto buildDirectoryPackPlan(
  std::filesystem::path const& dirPath,
  std::filesystem::path const& zipFileDir,
  std::uintmax_t maxGroupSize = pack::kDefaultMaxArchiveGroupSize,
  bool recursive = true,
  bool forceNameConflictHandling = false,
  std::optional<std::size_t> maxParallelJobs = std::nullopt,
  std::optional<std::filesystem::path> excludedPath = std::nullopt
) -> eh::Result<pack::PackPlan>;
