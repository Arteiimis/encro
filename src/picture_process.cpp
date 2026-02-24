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

auto packAllPicsToZipParallel(fs::path const& dirPath, fs::path const& zipFileDir)
  -> eh::Result<void> {
  std::println("Scanning input path for pictures: {} ...", dirPath.string());
  auto const groupedPics = groupFilesBySize(readAllPics(dirPath));
  std::println(
    "Picture scan completed, grouped into {} package batch(es).",
    groupedPics.size()
  );
  auto progressCtx = progress::ProgressContext{};
  auto packResults = std::vector<eh::Result<void>>(groupedPics.size());
  auto packResultsMtx = std::mutex{};

  const auto picCount = [&] {
    auto count = std::size_t{0};
    for (const auto& group: groupedPics) { count += group.size(); }
    return count;
  }();

  const auto proceed = readUserIpt(
    "do you want to proceed with packing the pictures? (y/N): "
  );
  if (!proceed) {
    std::println("Packing task canceled by user.");
    return eh::makeError("Packing task canceled by user.");
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

      auto const packRes = packFilesToZip(
        group,
        zipFilePath,
        progressCtx,
        std::format("Packing: {}", zipFileName)
      );

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
