#include <filesystem>
#include <array>
#include <expected>
#include <ranges>

#include <BS_thread_pool.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/dynamic_progress.hpp>
#include <spdlog/spdlog.h>

#include "packer.h"
#include "globals.h"
#include "utils.h"
#include "picture_process.h"

namespace fs = std::filesystem;
using namespace indicators;

template<class Iter>
auto readAllPicsImpl(const fs::path& dirPath) -> std::vector<fs::path> {
  namespace rng = std::ranges;

  constexpr auto pictureTypes = std::array{
    // Common picture file extensions
    ".jpg",
    ".jpeg",
    ".png",
    ".bmp",
    ".tiff",
    ".gif",
    ".webp",
    ".heic"
  };

  auto pics = std::vector<fs::path>{};

  for (const auto& entry: Iter{dirPath}) {
    if (!fs::is_regular_file(entry.path())) {
      spdlog::debug("Skipping non-regular file: {}", entry.path().string());
      continue;
    }

    const auto ext = entry.path().extension().string();
    if (rng::contains(pictureTypes, ext)) { pics.emplace_back(entry.path()); }
  }

  return pics;
}

auto readAllPics(const fs::path& dirPath) -> std::vector<fs::path> {
  if (GLBs.RECURSIVE) {
    return readAllPicsImpl<fs::recursive_directory_iterator>(dirPath);
  } else {
    return readAllPicsImpl<fs::directory_iterator>(dirPath);
  }
}

auto packAllPicsToZipParallel(
  const std::filesystem::path& dirPath,
  const std::filesystem::path& zipFileDir
) -> eh::Result<void> {
  namespace view = std::views;

  const auto groupedPics = groupFilesBySize(readAllPics(dirPath));
  auto pool = BS::pause_thread_pool{groupedPics.size()};
  pool.pause();
  auto bars = std::vector<std::unique_ptr<indicators::ProgressBar>>{};
  auto progressManager = indicators::DynamicProgress<indicators::ProgressBar>{};

  for (const auto& [index, _]: view::enumerate(groupedPics)) {
    bars.emplace_back(getProgressBar(
      std::format("Packing: {}_part{}.zip", dirPath.filename().string(), index + 1)
    ));
    progressManager.push_back(*bars.back());
  }

  auto packResults = std::vector<eh::Result<void>>(groupedPics.size());

  for (const auto& [index, group]: view::enumerate(groupedPics)) {
    pool.detach_task([&, index, group] {
      const auto zipFileName = std::format(
        "{}_part{}.zip",
        dirPath.filename().string(),
        index + 1
      );
      const auto zipFilePath = zipFileDir / zipFileName;
      fs::create_directory(zipFileDir);

      const auto packRes = packFilesToZip(group, zipFilePath, progressManager, index);
      if (!packRes) {
        fs::remove(zipFilePath);

        const auto errMsg = std::format(
          "Failed to pack pictures to {}: {}",
          zipFilePath.string(),
          packRes.error()
        );
        spdlog::error(errMsg);
        packResults[index] = eh::makeError("{}", errMsg);
        return;
      }

      packResults[index] = {};
    });
  }

  cursorToggleVisibility(false);
  pool.unpause();
  pool.wait();
  cursorToggleVisibility(true);

  for (const auto& res: packResults) {
    if (!res) { return res; }
  }

  return {};
}
