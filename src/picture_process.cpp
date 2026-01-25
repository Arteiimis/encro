#include <fstream>
#include <filesystem>
#include <array>
#include <expected>
#include <ranges>

#include <spdlog/spdlog.h>

#include "packer.h"
#include "globals.h"
#include "utils.h"

namespace fs = std::filesystem;

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

auto createZipFile(const fs::path& zipFilePath) -> std::expected<void, std::string> {
  auto res = exec2(std::format("7z a -tzip \"{}\"", zipFilePath.string()));
  if (res.first != 0) {
    return std::unexpected(
      std::format(
        "Failed to create zip file {}: {}",
        zipFilePath.string(),
        res.second
      )
    );
  }

  return {};
}

auto packAllPicsToZip(const fs::path& dirPath, const fs::path& zipFileDir)
  -> std::expected<void, std::string> {
  namespace view = std::views;

  const auto groupedPics = groupFilesBySize(readAllPics(dirPath));

  for (const auto& [index, group]: view::enumerate(groupedPics)) {
    const auto zipFileName = std::format(
      "{}_part{}.zip",
      dirPath.filename().string(),
      index + 1
    );
    const auto zipFilePath = zipFileDir / zipFileName;
    fs::create_directory(zipFileDir);

    const auto createRes = createZipFile(zipFilePath);
    if (!createRes) {
      spdlog::error(createRes.error());
      return createRes;
    }

    const auto packRes = packFilesToZip(group, zipFilePath);
    if (!packRes) {
      fs::remove(zipFilePath);

      const auto errMsg = std::format(
        "Failed to pack pictures to {}: {}",
        zipFilePath.string(),
        packRes.error()
      );
      spdlog::error(errMsg);
      return std::unexpected{errMsg};
    }
  }

  return {};
}
