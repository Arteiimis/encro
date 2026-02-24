#pragma once

#include <filesystem>
#include <vector>

#include "error_handle.h"
#include "progress.h"

auto packFilesToZip(
  const std::vector<std::filesystem::path>& filePaths,
  const std::filesystem::path& zipFilePath,
  progress::ProgressContext& progressCtx,
  std::string_view progressText
) -> eh::Result<void>;

auto groupFilesBySize(
  const std::vector<std::filesystem::path>& filePaths,
  std::uintmax_t maxGroupSize = 490 * 1024 * 1024
) -> std::vector<std::vector<std::filesystem::path>>;

auto packAllFilesInDirectory(
  std::filesystem::path const& dirPath,
  std::filesystem::path const& zipFileDir,
  std::uintmax_t maxGroupSize = 500 * 1024 * 1024,
  bool recursive = true
) -> eh::Result<void>;
