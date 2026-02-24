#pragma once

#include <filesystem>
#include <vector>

#include <indicators/progress_bar.hpp>
#include <indicators/dynamic_progress.hpp>

#include "error_handle.h"

auto packFilesToZip(
  const std::vector<std::filesystem::path>& filePaths,
  const std::filesystem::path& zipFilePath,
  indicators::DynamicProgress<indicators::ProgressBar>& progressBarManager,
  size_t progressBarIndex
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
