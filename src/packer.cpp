#include <expected>
#include <filesystem>
#include <ranges>

#include <libzippp/libzippp.h>
#include <indicators/progress_bar.hpp>

namespace fs = std::filesystem;

auto packFilesToZip(
  const std::vector<fs::path>& filePaths,
  const fs::path&              zipFilePath,
  indicators::ProgressBar*     progressBar = nullptr
) -> std::expected<void, std::string> try {
  auto zip = libzippp::ZipArchive(zipFilePath.string());
  auto fileCount = filePaths.size();

  zip.open(libzippp::ZipArchive::New);

  for (const auto& [index, filePath]: std::views::enumerate(filePaths)) {
    if (fs::is_regular_file(filePath)) {
      zip.addFile(filePath.filename().string(), filePath.string());
      if (progressBar) {
        progressBar->set_progress(index / (float)fileCount * 100.0f);
      }
    }
  }

  zip.close();

  return {};
} catch (const std::exception& e) {

  return std::unexpected(
    std::format("Failed to create zip archive {}: {}", zipFilePath.string(), e.what())
  );
}

auto groupFilesBySize(
  const std::vector<fs::path>& filePaths,
  std::uintmax_t               maxGroupSize = 490 * 1024 * 1024
) -> std::vector<std::vector<fs::path>> {
  auto groupedFiles = std::vector<std::vector<fs::path>>{};
  auto currentGroup = std::vector<fs::path>{};
  auto currentSize  = std::uintmax_t{0};

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
