#include <filesystem>
#include <ranges>
#include <cmath>

#include <spdlog/spdlog.h>
#include <libzippp/libzippp.h>
#include <indicators/progress_bar.hpp>
#include <indicators/dynamic_progress.hpp>

#include "packer.h"

namespace fs = std::filesystem;
using namespace indicators;

auto packFilesToZip(
  const std::vector<fs::path>& filePaths,
  const fs::path& zipFilePath,
  DynamicProgress<ProgressBar>& progressBarManager,
  size_t progressBarIndex
) -> eh::Result<void> try {
  auto zip = libzippp::ZipArchive(zipFilePath.string());
  auto fileCount = filePaths.size();

  zip.open(libzippp::ZipArchive::New);

  for (const auto& [index, filePath]: std::views::enumerate(filePaths)) {
    const auto progress = (size_t)std::round((index + 1) / (float)fileCount * 100.0f);

    if (fs::is_regular_file(filePath)) {
      zip.addFile(filePath.filename().string(), filePath.string());
      progressBarManager[progressBarIndex].set_progress(progress);
    }

    spdlog::debug("Packing progress: {}%, File: {}", progress, filePath.string());
  }

  zip.close();

  return {};
} catch (const std::exception& e) {

  return eh::makeError(
    "Exception while packing files to zip {}: {}",
    zipFilePath.string(),
    e.what()
  );
}

auto groupFilesBySize(
  const std::vector<fs::path>& filePaths,
  std::uintmax_t maxGroupSize
) -> std::vector<std::vector<fs::path>> {
  auto groupedFiles = std::vector<std::vector<fs::path>>{};
  auto currentGroup = std::vector<fs::path>{};
  auto currentSize = std::uintmax_t{0};

  for (const auto& filePath: filePaths) {
    const auto fileSize = fs::file_size(filePath);

    if (currentSize + fileSize > maxGroupSize && !currentGroup.empty()) {
      groupedFiles.emplace_back(currentGroup);
      currentGroup.clear();
      currentSize = 0;
    }

    currentGroup.emplace_back(filePath);
    currentSize += fileSize;
  }

  if (!currentGroup.empty()) { groupedFiles.emplace_back(currentGroup); }

  return groupedFiles;
}
