#include "packer.h"

#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>
#include <libzippp/libzippp.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <filesystem>
#include <ranges>

namespace fs = std::filesystem;
using namespace indicators;

auto packFilesToZip(
  std::vector<fs::path> const& filePaths,
  fs::path const& zipFilePath,
  DynamicProgress<ProgressBar>& progressBarManager,
  size_t progressBarIndex
) -> eh::Result<void> try {
  auto zip = libzippp::ZipArchive(zipFilePath.string());
  auto fileCount = filePaths.size();

  zip.open(libzippp::ZipArchive::New);

  for (auto const& [index, filePath]: std::views::enumerate(filePaths)) {
    auto const progress =
      (size_t)std::round((index + 1) / (float)fileCount * 100.0f);

    if (fs::is_regular_file(filePath)) {
      zip.addFile(filePath.filename().string(), filePath.string());
      progressBarManager[progressBarIndex].set_progress(progress);
    }

    spdlog::debug("Packing progress: {}%, File: {}", progress, filePath.string());
  }

  zip.close();

  return {};
} catch (std::exception const& e) {

  return eh::makeError(
    "Exception while packing files to zip {}: {}",
    zipFilePath.string(),
    e.what()
  );
}

auto groupFilesBySize(
  std::vector<fs::path> const& filePaths,
  std::uintmax_t maxGroupSize
) -> std::vector<std::vector<fs::path>> {
  auto groupedFiles = std::vector<std::vector<fs::path>>{};
  auto currentGroup = std::vector<fs::path>{};
  auto currentSize = std::uintmax_t{0};

  for (auto const& filePath: filePaths) {
    auto const fileSize = fs::file_size(filePath);

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
