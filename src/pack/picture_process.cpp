#include "pack/picture_process.h"

#include "core/media_scanner.h"
#include "pack/pack_service.h"
#include "pack/packer.h"
#include "utils/utils.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <print>
#include <string_view>
#include <unordered_map>

namespace fs = std::filesystem;
using namespace std::literals;

namespace {

using PictureEntryPlan = std::unordered_map<fs::path, std::string>;

auto stablePathString(fs::path const& path) -> std::string {
  auto normalized = path.lexically_normal().generic_string();
  std::ranges::transform(normalized, normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return normalized;
}

auto fnv1a32(std::string_view text) -> std::uint32_t {
  auto hash = std::uint32_t{2166136261u};
  for (auto const ch: text) {
    hash ^= static_cast<unsigned char>(ch);
    hash *= 16777619u;
  }
  return hash;
}

auto shortPathHash(fs::path const& path) -> std::string {
  return std::format("{:08x}", fnv1a32(stablePathString(path)));
}

auto sanitizeLabel(std::string_view text) -> std::string {
  auto sanitized = std::string{};
  sanitized.reserve(text.size());

  auto lastWasSeparator = false;
  for (auto const ch: text) {
    if (std::isalnum(static_cast<unsigned char>(ch))) {
      sanitized.push_back(static_cast<char>(std::tolower(ch)));
      lastWasSeparator = false;
      continue;
    }

    if (!lastWasSeparator) {
      sanitized.push_back('_');
      lastWasSeparator = true;
    }
  }

  while (!sanitized.empty() && sanitized.front() == '_') {
    sanitized.erase(sanitized.begin());
  }
  while (!sanitized.empty() && sanitized.back() == '_') { sanitized.pop_back(); }

  return sanitized;
}

auto relativeParentPath(fs::path const& rootDir, fs::path const& filePath)
  -> std::optional<fs::path> {
  auto const relativePath = filePath.parent_path().lexically_relative(rootDir);
  if (relativePath.empty() || relativePath == fs::path{"."}) {
    return std::nullopt;
  }

  return relativePath;
}

auto buildPictureCollisionGroupLabel(
  fs::path const& dirPath,
  fs::path const& filePath
) -> std::string {
  auto label = std::string{};
  if (auto const relativePath = relativeParentPath(dirPath, filePath);
      relativePath.has_value()) {
    label = sanitizeLabel(relativePath->generic_string());
  }

  if (label.empty() && filePath.has_extension()) {
    auto const extension = filePath.extension().string();
    auto const extensionView =
      std::string_view{extension}.substr(extension.starts_with('.') ? 1 : 0);
    label = sanitizeLabel(extensionView);
  }

  if (label.empty()) { label = "src"; }

  return label;
}

auto shouldForcePictureConflictNaming(appctx::AppConfig const& config) -> bool {
  return config.forceNameConflictHandling
      && config.outputLayout == appctx::OutputLayout::Flat;
}

auto buildConflictHandledPictureEntryName(
  fs::path const& dirPath,
  fs::path const& filePath
) -> std::string {
  auto const stem = filePath.stem().string();
  auto const extension = filePath.extension().string();
  return std::format(
    "{}__{}__{}{}",
    buildPictureCollisionGroupLabel(dirPath, filePath),
    stem,
    shortPathHash(filePath),
    extension
  );
}

auto planPictureZipEntryNames(
  appctx::AppConfig const& config,
  fs::path const& dirPath,
  std::vector<fs::path> const& filePaths
) -> PictureEntryPlan {
  auto plannedEntries = PictureEntryPlan{};
  plannedEntries.reserve(filePaths.size());

  if (config.outputLayout == appctx::OutputLayout::Keep) {
    for (auto const& filePath: filePaths) {
      auto const relativePath = filePath.lexically_relative(dirPath);
      plannedEntries[filePath] =
        (relativePath.empty() || relativePath == fs::path{"."})
          ? filePath.filename().generic_string()
          : relativePath.generic_string();
    }
    return plannedEntries;
  }

  auto groupedCandidates = std::unordered_map<std::string, std::vector<fs::path>>{};
  groupedCandidates.reserve(filePaths.size());
  auto const forceConflictNaming = shouldForcePictureConflictNaming(config);
  for (auto const& filePath: filePaths) {
    groupedCandidates[filePath.filename().generic_string()].push_back(filePath);
  }

  for (auto const& [fileName, groupedPaths]: groupedCandidates) {
    if (groupedPaths.size() == 1 && !forceConflictNaming) {
      plannedEntries[groupedPaths.front()] = fileName;
      continue;
    }

    auto sortedPaths = groupedPaths;
    std::ranges::sort(sortedPaths, [](fs::path const& lhs, fs::path const& rhs) {
      return stablePathString(lhs) < stablePathString(rhs);
    });

    auto const fileNamePath = fs::path{fileName};
    for (auto const& filePath: sortedPaths) {
      plannedEntries[filePath] =
        buildConflictHandledPictureEntryName(dirPath, filePath);
    }
  }

  return plannedEntries;
}

}  // namespace

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
  constexpr auto kMaxPicturePackSize = std::uintmax_t{500ULL * 1024ULL * 1024ULL};
  constexpr auto kMaxPicturesPerPack = std::size_t{2000};

  std::println("Scanning input path for pictures: {} ...", dirPath.string());
  auto const scannedPics = readAllPics(config, dirPath);
  auto const plannedEntryNames =
    planPictureZipEntryNames(config, dirPath, scannedPics);
  auto const groupedPics =
    groupFilesBySize(scannedPics, kMaxPicturePackSize, kMaxPicturesPerPack);
  auto const ordinalRanges = pack::buildGroupOrdinalRanges(groupedPics);
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
      [dirName = dirPath.filename().string(), ordinalRanges](std::size_t index) {
        return pack::appendOrdinalRangeSuffix(
          std::format("{}_part{}.zip", dirName, index + 1),
          ordinalRanges.at(index)
        );
      },
    .progressLabelForIndex =
      [dirName = dirPath.filename().string(), ordinalRanges](std::size_t index) {
        return std::format(
          "Packing: {}",
          pack::appendOrdinalRangeSuffix(
            std::format("{}_part{}.zip", dirName, index + 1),
            ordinalRanges.at(index)
          )
        );
      },
    .zipEntryNameForFile =
      [plannedEntryNames](fs::path const& filePath) {
        if (auto const it = plannedEntryNames.find(filePath);
            it != plannedEntryNames.end()) {
          return it->second;
        }
        return filePath.filename().generic_string();
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
