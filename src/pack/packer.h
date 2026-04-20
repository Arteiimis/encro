#pragma once

#include "core/error_handle.h"
#include "core/progress.h"
#include "pack/pack_service.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

using ZipEntryNameResolver = std::function<std::string(std::filesystem::path const&)>;

struct PackGroupInput {
  std::filesystem::path filePath;
  std::filesystem::path sourceDir;
};

struct PackGroupPartition {
  std::vector<std::filesystem::path> filePaths;
  std::size_t partIndex = 0;
  std::size_t subPartIndex = 0;
};

struct PackEntryInput {
  pack::PackFileEntry entry;
  std::filesystem::path sourceDir;
  std::optional<std::string> sourceKey;
  std::optional<std::string> fileKey;
};

struct PackEntryPartition {
  std::vector<pack::PackFileEntry> entries;
  std::size_t partIndex = 0;
  std::size_t subPartIndex = 0;
};

auto packFilesToZip(
  std::vector<std::filesystem::path> const& filePaths,
  std::filesystem::path const& zipFilePath,
  progress::ProgressContext& progressCtx,
  std::string_view progressText,
  ZipEntryNameResolver entryNameForFile = {}
) -> eh::Result<void>;

auto packFilesToZip(
  std::vector<pack::PackFileEntry> const& entries,
  std::filesystem::path const& zipFilePath,
  progress::ProgressContext& progressCtx,
  std::string_view progressText
) -> eh::Result<void>;

auto groupFilesBySize(
  std::vector<std::filesystem::path> const& filePaths,
  std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
  std::optional<std::size_t> maxFilesPerGroup = std::nullopt
) -> std::vector<std::vector<std::filesystem::path>>;

auto groupPackFiles(
  std::vector<PackGroupInput> const& filePaths,
  std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
  std::optional<std::size_t> maxFilesPerGroup = std::nullopt,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
) -> std::vector<std::vector<std::filesystem::path>>;

auto groupPackFilesWithSubparts(
  std::vector<PackGroupInput> const& filePaths,
  std::uintmax_t maxGroupSize,
  std::size_t maxFilesPerPart,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
) -> std::vector<PackGroupPartition>;

auto groupPackEntries(
  std::vector<PackEntryInput> const& entries,
  std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
  std::optional<std::size_t> maxFilesPerGroup = std::nullopt,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
) -> std::vector<std::vector<pack::PackFileEntry>>;

auto groupPackEntriesWithSubparts(
  std::vector<PackEntryInput> const& entries,
  std::uintmax_t maxGroupSize,
  std::size_t maxFilesPerPart,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
) -> std::vector<PackEntryPartition>;

auto packAllFilesInDirectory(
  std::filesystem::path const& dirPath,
  std::filesystem::path const& zipFileDir,
  std::uintmax_t maxGroupSize = 500 * 1024 * 1024,
  bool recursive = true,
  bool forceNameConflictHandling = false,
  std::optional<std::size_t> maxParallelJobs = std::nullopt
) -> eh::Result<void>;

auto buildDirectoryPackPlan(
  std::filesystem::path const& dirPath,
  std::filesystem::path const& zipFileDir,
  std::uintmax_t maxGroupSize = 500 * 1024 * 1024,
  bool recursive = true,
  bool forceNameConflictHandling = false,
  std::optional<std::size_t> maxParallelJobs = std::nullopt,
  std::optional<std::filesystem::path> excludedPath = std::nullopt
) -> eh::Result<pack::PackPlan>;
