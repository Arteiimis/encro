#include "pack/picture_process.h"

#include "core/media_scanner.h"
#include "pack/pack_service.h"
#include "pack/packer.h"
#include "utils/utils.h"

#include <spdlog/spdlog.h>

#include <array>
#include <expected>
#include <filesystem>
#include <print>
#include <string_view>

namespace fs = std::filesystem;
using namespace std::literals;

auto readAllPics(appctx::AppConfig const& config, fs::path const& dirPath)
  -> std::vector<fs::path> {
  constexpr auto pictureTypes = std::array{
    // Common picture file extensions
    ".jpg"sv,
    ".jpeg"sv,
    ".png"sv,
    ".bmp"sv,
    ".tiff"sv,
    ".gif"sv,
    ".heic"sv
  };

  return media::scanByExtensions(dirPath, pictureTypes, config.recursive);
}

auto packAllPicsToZipParallel(
  appctx::AppConfig const& config,
  fs::path const& dirPath,
  fs::path const& zipFileDir
) -> eh::Result<void> {
  std::println("Scanning input path for pictures: {} ...", dirPath.string());
  auto const scannedPics = readAllPics(config, dirPath);
  auto const groupedPics = groupFilesBySize(scannedPics);
  std::println(
    "Picture scan completed, {} picture(s) found, grouped into {} package "
    "batch(es).",
    scannedPics.size(),
    groupedPics.size()
  );
  auto const proceed = readUserIpt(
    config.yesToAll,
    "do you want to proceed with packing the pictures? (y/N): "
  );
  if (!proceed) {
    std::println("Packing task canceled by user.");
    return eh::makeError("Packing task canceled by user.");
  }

  auto const plan = pack::PackPlan{
    .groups = groupedPics,
    .outputDir = zipFileDir,
    .zipNameForIndex =
      [dirName = dirPath.filename().string()](std::size_t index) {
        return std::format("{}_part{}.zip", dirName, index + 1);
      },
    .progressLabelForIndex =
      [dirName = dirPath.filename().string()](std::size_t index) {
        return std::format("Packing: {}_part{}.zip", dirName, index + 1);
      },
    .maxParallelJobs = config.maxParallelJobs,
    .removeOnFailure = true
  };

  auto const packRes = pack::packGroupsParallel(plan);
  if (!packRes) {
    auto const errMsg = std::format(
      "Failed to pack pictures in {}: {}",
      zipFileDir.string(),
      packRes.error()
    );
    spdlog::error(errMsg);
    return eh::makeError("{}", errMsg);
  }

  return {};
}
