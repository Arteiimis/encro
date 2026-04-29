#include "picture/picture_process.h"

#include "picture/picture_compress.h"

#include "core/collision_naming.h"
#include "core/media_scanner.h"
#include "infra/terminal.h"
#include "pack/pack_service.h"
#include "pack/packer.h"
#include "utils/utils.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace fs = std::filesystem;
using namespace std::literals;
namespace naming = collisionnaming;

using namespace pack::detail;

using enum terminal::MessageKind;

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

auto toJpgEntryName(std::string const& entryName) -> std::string {
  auto const stemEnd = entryName.rfind('.');
  if (stemEnd != std::string::npos) { return entryName.substr(0, stemEnd) + ".jpg"; }
  return entryName + ".jpg";
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

auto addCompressTask(
  std::unordered_map<fs::path, fs::path>& compressedSet,
  fs::path const& tempDir,
  std::error_code& ec,
  std::vector<CompressTask>& compressTasks,
  fs::path const& picPath,
  std::string const& entryName
) -> void {
  if (compressedSet.contains(picPath)) { return; }
  auto const jpgEntryName = toJpgEntryName(entryName);
  auto const outputPath = tempDir / jpgEntryName;
  fs::create_directories(outputPath.parent_path(), ec);

  compressTasks.push_back(
    CompressTask{
      .inputPath = picPath,
      .outputPath = outputPath,
      .entryName = jpgEntryName,
    }
  );
  compressedSet[picPath] = outputPath;
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

struct PreparedPicturePack {
  std::size_t pictureCount = 0;
  pack::PackPlan plan;
};

void printPicturePackWorkflowSummary(PreparedPicturePack const& prepared) {
  terminal::println(
    Info,
    "Picture scan completed, {} picture(s) found, grouped into {} package batch(es).",
    terminal::count(prepared.pictureCount),
    terminal::count(prepared.plan.groups.size())
  );
}

auto preparePicturePack(
  appctx::AppConfig const& config,
  fs::path const& dirPath,
  fs::path const& zipFileDir
) -> eh::Result<PreparedPicturePack> {
  terminal::println(
    Info,
    "Scanning input path for pictures: {} ...",
    terminal::path(dirPath)
  );
  auto const scannedPics = readAllPics(config, dirPath);
  auto const planRes = buildPicturePackPlan(config, dirPath, zipFileDir, scannedPics);
  if (!planRes) { return eh::makeError("{}", planRes.error()); }

  auto prepared = PreparedPicturePack{
    .pictureCount = scannedPics.size(),
    .plan = planRes.value(),
  };
  printPicturePackWorkflowSummary(prepared);
  return prepared;
}

auto confirmPicturePack(appctx::AppConfig const& config) -> bool {
  return readUserIpt(
    config.yesToAll,
    "do you want to proceed with packing the pictures? (y/N): "
  );
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

auto runPicturePackWorkflow(appctx::AppContext& ctx, fs::path const& dirPath)
  -> eh::Result<int> {
  auto const outputDir = ctx.config.outputPath.value_or(dirPath) / "packed";

  if (ctx.config.compressImages) {
    auto const scannedPics = readAllPics(ctx.config, dirPath);
    if (scannedPics.empty()) {
      return eh::makeError("No pictures found in directory: {}", dirPath.string());
    }

    auto const quality = ctx.config.imageQuality.value_or(5);
    terminal::println(
      Info,
      "Picture scan completed, {} picture(s) found, will be compressed to JPEG "
      "(quality={}).",
      terminal::count(scannedPics.size()),
      terminal::count(quality)
    );

    if (!confirmPicturePack(ctx.config)) {
      terminal::println(Warning, "Packing task canceled by user.");
      return 0;
    }

    auto const tempDir = outputDir / ".compress_tmp";
    auto ec = std::error_code{};
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir);

    auto summaryPics = std::vector<fs::path>{};
    if (ctx.config.pictureFolderSummary) {
      summaryPics = collectFolderSummaryPictures(dirPath, scannedPics);
    }

    auto plannedEntryNames = planPictureZipEntryNames(ctx.config, dirPath, scannedPics);

    auto compressedSet = std::unordered_map<fs::path, fs::path>{};
    auto compressTasks = std::vector<CompressTask>{};
    compressTasks.reserve(scannedPics.size() + summaryPics.size());

    for (auto const& summaryPic: summaryPics) {
      auto const entryName = buildSummaryPictureEntryName(dirPath, summaryPic);
      addCompressTask(compressedSet, tempDir, ec, compressTasks, summaryPic, entryName);
    }

    for (auto const& picPath: scannedPics) {
      auto const plannedIt = plannedEntryNames.find(picPath);
      auto const entryName = plannedIt != plannedEntryNames.end()
        ? plannedIt->second
        : picPath.filename().generic_string();
      addCompressTask(compressedSet, tempDir, ec, compressTasks, picPath, entryName);
    }

    terminal::println(
      Info,
      "Compressing {} picture(s) to JPEG (quality={})...",
      terminal::count(scannedPics.size()),
      terminal::count(quality)
    );

    auto const maxParallel = ctx.config.maxParallelJobs.value_or(10);
    auto const compressResults =
      compressImageBatch(ctx, compressTasks, quality, maxParallel);

    if (compressResults.empty()) {
      fs::remove_all(tempDir, ec);
      return eh::makeError("All picture compressions failed.");
    }

    terminal::println(
      Info,
      "{} picture(s) compressed, preparing pack plan...",
      terminal::count(compressResults.size())
    );

    auto packInputs = std::vector<PackEntryInput>{};
    packInputs.reserve(compressResults.size());

    for (auto const& summaryPic: summaryPics) {
      auto const compressedIt = compressedSet.find(summaryPic);
      if (compressedIt == compressedSet.end()) { continue; }

      auto const dirKey = naming::stablePathString(summaryPic.parent_path());
      auto const entryName =
        toJpgEntryName(buildSummaryPictureEntryName(dirPath, summaryPic));

      packInputs.emplace_back(
        PackEntryInput{
          .entry =
            pack::PackFileEntry{
              .sourcePath = compressedIt->second,
              .zipEntryName = entryName,
            },
          .sourceDir = summaryPic.parent_path(),
          .sourceKey = std::format("0000__{}", dirKey),
          .fileKey = std::format("0000__{}", dirKey),
        }
      );
    }

    for (auto const& picPath: scannedPics) {
      auto const compressedIt = compressedSet.find(picPath);
      if (compressedIt == compressedSet.end()) { continue; }

      auto const plannedIt = plannedEntryNames.find(picPath);
      auto const entryName = plannedIt != plannedEntryNames.end()
        ? toJpgEntryName(plannedIt->second)
        : toJpgEntryName(picPath.filename().generic_string());

      auto const sourceKey =
        std::format("1000__{}", naming::stablePathString(picPath.parent_path()));
      packInputs.emplace_back(
        PackEntryInput{
          .entry =
            pack::PackFileEntry{
              .sourcePath = compressedIt->second,
              .zipEntryName = entryName,
            },
          .sourceDir = picPath.parent_path(),
          .sourceKey = sourceKey,
          .fileKey = std::format("1000__{}", naming::stablePathString(picPath)),
        }
      );
    }

    constexpr auto kMaxPicturesPerPack = std::size_t{2000};
    constexpr auto kFolderCarryOverThreshold = std::size_t{2000};
    auto const groupedPartitions = groupPackEntriesWithSubparts(
      packInputs,
      pack::kDefaultMaxArchiveGroupSize,
      kMaxPicturesPerPack,
      kFolderCarryOverThreshold
    );

    auto groupedPics = std::vector<std::vector<pack::PackFileEntry>>{};
    auto groupNameParts = std::vector<std::pair<std::size_t, std::size_t>>{};
    auto subPartCountsByPart = std::vector<std::size_t>{};
    groupedPics.reserve(groupedPartitions.size());
    groupNameParts.reserve(groupedPartitions.size());
    subPartCountsByPart.reserve(groupedPartitions.size());
    for (auto const& partition: groupedPartitions) {
      groupedPics.emplace_back(partition.entries);
      groupNameParts.emplace_back(partition.partIndex, partition.subPartIndex);
      if (subPartCountsByPart.size() < partition.partIndex) {
        subPartCountsByPart.resize(partition.partIndex, 0);
      }
      ++subPartCountsByPart[partition.partIndex - 1];
    }

    auto const ordinalRanges = pack::buildGroupOrdinalRanges(groupedPics);
    auto picturePackNaming = PicturePackNamingState{
      .dirName = dirPath.filename().string(),
      .ordinalRanges = ordinalRanges,
      .groupNameParts = groupNameParts,
      .subPartCountsByPart = subPartCountsByPart,
    };
    auto const picturePackNamingState =
      std::make_shared<PicturePackNamingState>(std::move(picturePackNaming));

    auto const plan = pack::PackPlan{
      .groups = groupedPics,
      .outputDir = outputDir,
      .zipNameForIndex = [picturePackNamingState](
                           std::size_t index
                         ) { return picturePackNamingState->zipNameFor(index); },
      .maxParallelJobs = ctx.config.maxParallelJobs,
      .removeOnFailure = true,
      .compact = true
    };

    auto const packRes = pack::runPackPlan(ctx, plan);
    fs::remove_all(tempDir, ec);

    if (!packRes) {
      return eh::makeError("Failed to pack pictures: {}", packRes.error());
    }
    if (packRes->exitCode != 0) { return packRes->exitCode; }

    terminal::println(
      Success,
      "All pictures packed successfully to: {}",
      terminal::path(outputDir)
    );
    return 0;
  }

  auto const preparedRes = preparePicturePack(ctx.config, dirPath, outputDir);
  if (!preparedRes) {
    return eh::makeError("Failed to pack pictures: {}", preparedRes.error());
  }

  auto const& prepared = preparedRes.value();
  if (!confirmPicturePack(ctx.config)) {
    terminal::println(Warning, "Packing task canceled by user.");
    return 0;
  }

  auto const packRes = pack::runPackPlan(ctx, prepared.plan);
  if (!packRes) { return eh::makeError("Failed to pack pictures: {}", packRes.error()); }
  if (packRes->exitCode != 0) { return packRes->exitCode; }

  terminal::println(
    Success,
    "All pictures packed successfully to: {}",
    terminal::path(outputDir)
  );
  return 0;
}

auto packAllPicsToZip(
  appctx::AppConfig const& config,
  fs::path const& dirPath,
  fs::path const& zipFileDir
) -> eh::Result<void> {
  auto const preparedRes = preparePicturePack(config, dirPath, zipFileDir);
  if (!preparedRes) { return eh::makeError("{}", preparedRes.error()); }

  auto const& prepared = preparedRes.value();
  if (!confirmPicturePack(config)) {
    terminal::println(Warning, "Packing task canceled by user.");
    return eh::makeError("Packing task canceled by user.");
  }

  auto const packRes = pack::packGroups(prepared.plan);
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
    pack::kDefaultMaxArchiveGroupSize,
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
  auto picturePackNaming = PicturePackNamingState{
    .dirName = dirPath.filename().string(),
    .ordinalRanges = ordinalRanges,
    .groupNameParts = groupNameParts,
    .subPartCountsByPart = subPartCountsByPart,
  };
  auto const picturePackNamingState =
    std::make_shared<PicturePackNamingState>(std::move(picturePackNaming));

  return pack::PackPlan{
    .groups = groupedPics,
    .outputDir = zipFileDir,
    .zipNameForIndex =  //
    [picturePackNamingState](std::size_t index) {
      return picturePackNamingState->zipNameFor(index);
    },
    .maxParallelJobs = config.maxParallelJobs,
    .removeOnFailure = true,
    .compact = true
  };
}
