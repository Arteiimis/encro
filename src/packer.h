#pragma once

#include <filesystem>
#include <expected>
#include <vector>

#include <indicators/progress_bar.hpp>

auto packFilesToZip(
  const std::vector<std::filesystem::path>& filePaths,
  const std::filesystem::path&              zipFilePath,
  indicators::ProgressBar*                  progressBar = nullptr
) -> std::expected<void, std::string>;

auto groupFilesBySize(
  const std::vector<std::filesystem::path>& filePaths,
  std::uintmax_t                            maxGroupSize = 490 * 1024 * 1024
) -> std::vector<std::vector<std::filesystem::path>>;
