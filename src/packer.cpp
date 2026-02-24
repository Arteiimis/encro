#include "packer.h"

#include "parallel.h"
#include "progress.h"

#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>
#include <libzippp/libzippp.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <filesystem>
#include <mutex>
#include <print>
#include <ranges>

namespace fs = std::filesystem;
using namespace indicators;

auto packFilesToZip(
  std::vector<fs::path> const& filePaths,
  fs::path const& zipFilePath,
  progress::ProgressContext& progressCtx,
  std::string_view progressText
) -> eh::Result<void> try {
  auto zip = libzippp::ZipArchive(zipFilePath.string());
  auto fileCount = filePaths.size();
  auto const progressBarIndex = progressCtx.addBar(progressText);

  zip.open(libzippp::ZipArchive::New);

  for (auto const& [index, filePath]: std::views::enumerate(filePaths)) {
    auto const progress =
      (size_t)std::round((index + 1) / (float)fileCount * 100.0f);

    if (fs::is_regular_file(filePath)) {
      zip.addFile(filePath.filename().string(), filePath.string());
      progressCtx.setProgress(progressBarIndex, static_cast<float>(progress));
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

auto packAllFilesInDirectory(
  fs::path const& dirPath,
  fs::path const& zipFileDir,
  std::uintmax_t maxGroupSize,
  bool recursive
) -> eh::Result<void> {
  if (!fs::is_directory(dirPath)) {
    return eh::makeError("Input path is not a directory: {}", dirPath.string());
  }

  auto allFiles = std::vector<fs::path>{};

  std::println(
    "Scanning input path for files: {} (recursive={})...",
    dirPath.string(),
    recursive ? "true" : "false"
  );

  if (recursive) {
    for (auto const& entry: fs::recursive_directory_iterator(dirPath)) {
      if (entry.is_regular_file()) { allFiles.emplace_back(entry.path()); }
    }
  } else {
    for (auto const& entry: fs::directory_iterator(dirPath)) {
      if (entry.is_regular_file()) { allFiles.emplace_back(entry.path()); }
    }
  }

  if (allFiles.empty()) {
    return eh::makeError(
      "No files found to pack in directory: {}",
      dirPath.string()
    );
  }

  std::println("File scan completed, found {} file(s).", allFiles.size());

  auto const groupedFiles = groupFilesBySize(allFiles, maxGroupSize);
  fs::create_directories(zipFileDir);

  auto progressCtx = progress::ProgressContext{};
  auto packResults = std::vector<eh::Result<void>>(groupedFiles.size());
  auto packResultsMtx = std::mutex{};

  auto _ = progress::CursorGuard{};
  parallel::runIndexedTasks(
    groupedFiles.size(),
    groupedFiles.size(),
    [&](std::size_t index) {
      auto const fileName =
        std::format("{}_part{}.zip", dirPath.filename().string(), index + 1);
      auto const zipPath = zipFileDir / fileName;

      auto const packRes =
        packFilesToZip(
          groupedFiles[index],
          zipPath,
          progressCtx,
          std::format("Packing: {}", fileName)
        );

      auto lock = std::scoped_lock{packResultsMtx};
      if (!packRes) {
        packResults[index] = packRes;
        return;
      }

      packResults[index] = {};
    }
  );

  for (auto const& res: packResults) {
    if (!res) { return res; }
  }

  return {};
}
