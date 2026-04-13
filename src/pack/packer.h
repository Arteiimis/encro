#pragma once

#include "core/error_handle.h"
#include "core/progress.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>


using ZipEntryNameResolver =
  std::function<std::string(std::filesystem::path const&)>;

struct PackGroupInput {
  std::filesystem::path filePath;
  std::filesystem::path sourceDir;
};

auto packFilesToZip(
  const std::vector<std::filesystem::path>& filePaths,
  const std::filesystem::path& zipFilePath,
  progress::ProgressContext& progressCtx,
  std::string_view progressText,
  ZipEntryNameResolver entryNameForFile = {}
) -> eh::Result<void>;

auto groupFilesBySize(
  const std::vector<std::filesystem::path>& filePaths,
  std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
  std::optional<std::size_t> maxFilesPerGroup = std::nullopt
) -> std::vector<std::vector<std::filesystem::path>>;

auto groupPackFiles(
  const std::vector<PackGroupInput>& filePaths,
  std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
  std::optional<std::size_t> maxFilesPerGroup = std::nullopt,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed =
    std::nullopt
) -> std::vector<std::vector<std::filesystem::path>>;

auto packAllFilesInDirectory(
  std::filesystem::path const& dirPath,
  std::filesystem::path const& zipFileDir,
  std::uintmax_t maxGroupSize = 500 * 1024 * 1024,
  bool recursive = true,
  bool forceNameConflictHandling = false,
  std::optional<std::size_t> maxParallelJobs = std::nullopt
) -> eh::Result<void>;
