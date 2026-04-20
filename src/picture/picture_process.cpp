#include "picture/picture_process.h"

#include "core/collision_naming.h"
#include "core/media_scanner.h"
#include "pack/pack_service.h"
#include "pack/packer.h"
#include "utils/utils.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <print>
#include <unordered_map>

namespace fs = std::filesystem;
using namespace std::literals;
namespace naming = collisionnaming;

namespace {

using PictureEntryPlan = std::unordered_map<fs::path, std::string>;

auto buildFlatPictureEntryName(std::string_view entryName) -> std::string {
  return std::format("1000__{}", entryName);
}

auto buildSummaryPictureEntryName(fs::path const& dirPath, fs::path const& filePath)
  -> std::string {
  return std::format(
    "0000__summary__{}__{}",
    naming::buildCollisionGroupPrefix(dirPath, filePath),
    filePath.filename().generic_string()
  );
}

auto shouldForcePictureConflictNaming(appctx::AppConfig const& config) -> bool {
  return config.forceNameConflictHandling
    && config.outputLayout == appctx::OutputLayout::Flat;
}

auto buildConflictHandledPictureEntryName(
  fs::path const& dirPath,
  fs::path const& filePath
) -> std::string {
  return naming::buildConflictHandledFlatName(
    dirPath,
    filePath,
    filePath.stem().string(),
    filePath.extension().string()
  );
}

auto planPictureZipEntryNames(
  appctx::AppConfig const& config,
  fs::path const& dirPath,
  std::span<fs::path const> filePaths
) -> PictureEntryPlan {
  auto plannedEntries = PictureEntryPlan{};
  plannedEntries.reserve(filePaths.size());

  if (config.outputLayout == appctx::OutputLayout::Keep) {
    for (auto const& filePath: filePaths) {
      auto const relativePath = filePath.lexically_relative(dirPath);
      plannedEntries[filePath] = (relativePath.empty() || relativePath == fs::path{"."})
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
      plannedEntries[groupedPaths.front()] = buildFlatPictureEntryName(fileName);
      continue;
    }

    auto sortedPaths = groupedPaths;
    std::ranges::sort(sortedPaths, [](fs::path const& lhs, fs::path const& rhs) {
      return naming::stablePathString(lhs) < naming::stablePathString(rhs);
    });

    for (auto const& filePath: sortedPaths) {
      plannedEntries[filePath] = buildFlatPictureEntryName(
        buildConflictHandledPictureEntryName(dirPath, filePath)
      );
    }
  }

  return plannedEntries;
}

auto collectFolderSummaryPictures(
  fs::path const& dirPath,
  std::span<fs::path const> filePaths
) -> std::vector<fs::path> {
  auto picturesByDirKey = std::unordered_map<std::string, std::vector<fs::path>>{};
  picturesByDirKey.reserve(filePaths.size());

  for (auto const& filePath: filePaths) {
    auto const relativeParent = filePath.parent_path().lexically_relative(dirPath);
    if (relativeParent.empty() || relativeParent == fs::path{"."}) { continue; }

    auto const dirKey = naming::stablePathString(filePath.parent_path());
    picturesByDirKey[dirKey].push_back(filePath);
  }

  auto sortedDirKeys = std::vector<std::string>{};
  sortedDirKeys.reserve(picturesByDirKey.size());
  for (auto const& [dirKey, _]: picturesByDirKey) { sortedDirKeys.push_back(dirKey); }
  std::ranges::sort(sortedDirKeys);

  auto summaryPictures = std::vector<fs::path>{};
  summaryPictures.reserve(sortedDirKeys.size());
  for (auto const& dirKey: sortedDirKeys) {
    auto pictures = picturesByDirKey.at(dirKey);
    std::ranges::sort(pictures, [](fs::path const& lhs, fs::path const& rhs) {
      return naming::stablePathString(lhs) < naming::stablePathString(rhs);
    });
    summaryPictures.push_back(pictures.front());
  }

  return summaryPictures;
}

auto buildPicturePackEntryInputs(
  appctx::AppConfig const& config,
  fs::path const& dirPath,
  std::span<fs::path const> scannedPics,
  PictureEntryPlan const& plannedEntryNames
) -> std::vector<PackEntryInput> {
  auto summaryPics = std::vector<fs::path>{};
  if (config.pictureFolderSummary) {
    summaryPics = collectFolderSummaryPictures(dirPath, scannedPics);
  }

  auto packInputs = std::vector<PackEntryInput>{};
  packInputs.reserve(scannedPics.size() + summaryPics.size());

  for (auto const& summaryPic: summaryPics) {
    auto const dirKey = naming::stablePathString(summaryPic.parent_path());
    packInputs.emplace_back(
      PackEntryInput{
        .entry =
          pack::PackFileEntry{
            .sourcePath = summaryPic,
            .zipEntryName = buildSummaryPictureEntryName(dirPath, summaryPic),
          },
        .sourceDir = summaryPic.parent_path(),
        .sourceKey = std::format("0000__{}", dirKey),
        .fileKey = std::format("0000__{}", dirKey),
      }
    );
  }

  for (auto const& picPath: scannedPics) {
    auto const plannedNameIt = plannedEntryNames.find(picPath);
    auto const entryName = plannedNameIt != plannedEntryNames.end()
      ? plannedNameIt->second
      : picPath.filename().generic_string();
    auto const sourceKey =
      std::format("1000__{}", naming::stablePathString(picPath.parent_path()));
    packInputs.emplace_back(
      PackEntryInput{
        .entry =
          pack::PackFileEntry{
            .sourcePath = picPath,
            .zipEntryName = entryName,
          },
        .sourceDir = picPath.parent_path(),
        .sourceKey = sourceKey,
        .fileKey = std::format("1000__{}", naming::stablePathString(picPath)),
      }
    );
  }

  return packInputs;
}

auto buildPicturePackBaseName(
  std::string const& dirName,
  std::size_t partIndex,
  std::size_t subPartIndex,
  std::size_t totalSubParts
) -> std::string {
  if (totalSubParts <= 1) { return std::format("{}_part{}.zip", dirName, partIndex); }

  return std::format("{}_part{}.{}.zip", dirName, partIndex, subPartIndex + 1);
}

struct PicturePackNamingState {
  std::string dirName;
  std::vector<pack::FileOrdinalRange> ordinalRanges;
  std::vector<std::pair<std::size_t, std::size_t>> groupNameParts;
  std::vector<std::size_t> subPartCountsByPart;

  auto zipNameFor(std::size_t index) const -> std::string {
    auto const [partIndex, subPartIndex] = groupNameParts.at(index);
    return pack::appendOrdinalRangeSuffix(
      buildPicturePackBaseName(
        dirName,
        partIndex,
        subPartIndex,
        subPartCountsByPart.at(partIndex - 1)
      ),
      ordinalRanges.at(index)
    );
  }
};

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
  std::println("Scanning input path for pictures: {} ...", dirPath.string());
  auto const scannedPics = readAllPics(config, dirPath);
  auto const planRes = buildPicturePackPlan(config, dirPath, zipFileDir, scannedPics);
  if (!planRes) { return eh::makeError("{}", planRes.error()); }

  std::println(
    "Picture scan completed, {} picture(s) found, grouped into {} package "
    "batch(es).",
    scannedPics.size(),
    planRes->groups.size()
  );
  auto const proceed = readUserIpt(
    config.yesToAll,
    "do you want to proceed with packing the pictures? (y/N): "
  );
  if (!proceed) {
    std::println("Packing task canceled by user.");
    return eh::makeError("Packing task canceled by user.");
  }

  auto const packRes = pack::packGroupsParallel(planRes.value());
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

auto buildPicturePackPlan(
  appctx::AppConfig const& config,
  fs::path const& dirPath,
  fs::path const& zipFileDir
) -> eh::Result<pack::PackPlan> {
  auto const scannedPics = readAllPics(config, dirPath);
  return buildPicturePackPlan(config, dirPath, zipFileDir, scannedPics);
}

auto buildPicturePackPlan(
  appctx::AppConfig const& config,
  fs::path const& dirPath,
  fs::path const& zipFileDir,
  std::span<fs::path const> scannedPics
) -> eh::Result<pack::PackPlan> {
  constexpr auto kMaxPicturePackSize = std::uintmax_t{500ULL * 1024ULL * 1024ULL};
  constexpr auto kMaxPicturesPerPack = std::size_t{2000};
  constexpr auto kFolderCarryOverThreshold = std::size_t{2000};

  if (scannedPics.empty()) {
    return eh::makeError("No pictures found to pack in directory: {}", dirPath.string());
  }

  auto const plannedEntryNames = planPictureZipEntryNames(config, dirPath, scannedPics);
  auto const packInputs =
    buildPicturePackEntryInputs(config, dirPath, scannedPics, plannedEntryNames);
  auto const groupedPicPartitions = groupPackEntriesWithSubparts(
    packInputs,
    kMaxPicturePackSize,
    kMaxPicturesPerPack,
    kFolderCarryOverThreshold
  );
  auto groupedPics = std::vector<std::vector<pack::PackFileEntry>>{};
  auto groupNameParts = std::vector<std::pair<std::size_t, std::size_t>>{};
  auto subPartCountsByPart = std::vector<std::size_t>{};
  groupedPics.reserve(groupedPicPartitions.size());
  groupNameParts.reserve(groupedPicPartitions.size());
  subPartCountsByPart.reserve(groupedPicPartitions.size());
  for (auto const& partition: groupedPicPartitions) {
    groupedPics.emplace_back(partition.entries);
    groupNameParts.emplace_back(partition.partIndex, partition.subPartIndex);
    if (subPartCountsByPart.size() < partition.partIndex) {
      subPartCountsByPart.resize(partition.partIndex, 0);
    }
    ++subPartCountsByPart[partition.partIndex - 1];
  }
  auto const ordinalRanges = pack::buildGroupOrdinalRanges(groupedPics);
  auto picturePackNaming = PicturePackNamingState{};
  picturePackNaming.dirName = dirPath.filename().string();
  picturePackNaming.ordinalRanges = ordinalRanges;
  picturePackNaming.groupNameParts = groupNameParts;
  picturePackNaming.subPartCountsByPart = subPartCountsByPart;
  auto const picturePackNamingState =
    std::make_shared<PicturePackNamingState>(std::move(picturePackNaming));

  return pack::PackPlan{
    .groups = groupedPics,
    .outputDir = zipFileDir,
    .zipNameForIndex = [picturePackNamingState](
                         std::size_t index
                       ) { return picturePackNamingState->zipNameFor(index); },
    .maxParallelJobs = config.maxParallelJobs,
    .removeOnFailure = true
  };
}
