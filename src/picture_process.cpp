#include "picture_process.h"

#include "globals.h"
#include "packer.h"
#include "parallel.h"
#include "progress.h"
#include "utils.h"

#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <expected>
#include <filesystem>
#include <print>
#include <ranges>

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

auto packAllPicsToZipParallel(const fs::path& dirPath, const fs::path& zipFileDir)
  -> eh::Result<void> {
  namespace view = std::views;

  auto const groupedPics = groupFilesBySize(readAllPics(dirPath));
  auto bars = std::vector<std::unique_ptr<indicators::ProgressBar>>{};
  auto progressManager = indicators::DynamicProgress<indicators::ProgressBar>{};
  auto packResults = std::vector<eh::Result<void>>(groupedPics.size());
  auto packResultsMtx = std::mutex{};

  const auto picCount = [&] {
    auto count = std::size_t{0};
    for (const auto& group: groupedPics) { count += group.size(); }
    return count;
  }();

  std::println(
    "Found {} pictures to pack in directory: {}, packing into {} zip files.",
    picCount,
    dirPath.string(),
    groupedPics.size()
  );
  const auto proceed = readUserIpt(
    "do you want to proceed with packing the pictures? (y/N): "
  );
  if (!proceed) {
    std::println("Packing task canceled by user.");
    return eh::makeError("Packing task canceled by user.");
  }

  for (const auto& [index, group]: view::enumerate(groupedPics)) {
    progress::addBar(
      progressManager,
      bars,
      std::format("Packing: {}_part{}.zip", dirPath.filename().string(), index + 1)
    );
  }

  auto _ = progress::CursorGuard{};
  parallel::runIndexedTasks(
    groupedPics.size(),
    groupedPics.size(),
    [&](std::size_t index) {
      auto const& group = groupedPics[index];
      const auto zipFileName = std::format(
        "{}_part{}.zip",
        dirPath.filename().string(),
        index + 1
      );
      const auto zipFilePath = zipFileDir / zipFileName;
      fs::create_directory(zipFileDir);

      auto const packRes =
        packFilesToZip(group, zipFilePath, progressManager, index);

      auto lock = std::scoped_lock{packResultsMtx};
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
    }
  );

  for (const auto& res: packResults) {
    if (!res) { return res; }
  }

  return {};
}
