#include "picture/picture_process.h"

#include "picture/picture_compress.h"

#include "core/collision_naming.h"
#include "core/media_scanner.h"
#include "infra/terminal.h"
#include "pack/pack.h"
#include "pack/pack_internal.h"
#include "pack/packer.h"
#include "pack/packer_types.h"
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

using enum terminal::MessageKind;

namespace {

using PictureEntryPlan = std::unordered_map<fs::path, std::string>;

constexpr auto kMaxPicturesPerPack = std::size_t{2000};

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

auto buildCompressTaskKey(fs::path const& inputPath, std::string_view entryName)
  -> std::string {
  return std::format("{}\n{}", naming::stablePathString(inputPath), entryName);
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

auto makePictureSummaryPackEntry(
  fs::path const& picPath,
  fs::path const& packedSourcePath,
  std::string const& entryName
) -> pack::detail::PackEntryInput {
  auto const dirKey = naming::stablePathString(picPath.parent_path());
  return pack::detail::PackEntryInput{
    .entry =
      pack::PackFileEntry{
        .sourcePath = packedSourcePath,
        .zipEntryName = entryName,
        .isSummary = true,
      },
    .sourceDir = picPath.parent_path(),
    .sourceKey = dirKey,
    .fileKey = dirKey,
    .isSummary = true,
  };
}

auto makePictureRegularPackEntry(
  fs::path const& picPath,
  fs::path const& packedSourcePath,
  std::string const& entryName
) -> pack::detail::PackEntryInput {
  auto const dirKey = naming::stablePathString(picPath.parent_path());
  return pack::detail::PackEntryInput{
    .entry =
      pack::PackFileEntry{
        .sourcePath = packedSourcePath,
        .zipEntryName = entryName,
      },
    .sourceDir = picPath.parent_path(),
    .sourceKey = dirKey,
    .fileKey = naming::stablePathString(picPath),
  };
}

auto buildPicturePackEntryInputs(
  appctx::AppConfig const& config,
  fs::path const& dirPath,
  std::span<fs::path const> scannedPics,
  PictureEntryPlan const& plannedEntryNames
) -> std::vector<pack::detail::PackEntryInput> {
  auto summaryPics = std::vector<fs::path>{};
  if (config.pictureFolderSummary) {
    summaryPics = collectFolderSummaryPictures(dirPath, scannedPics);
  }

  auto packInputs = std::vector<pack::detail::PackEntryInput>{};
  packInputs.reserve(scannedPics.size() + summaryPics.size());

  for (auto const& summaryPic: summaryPics) {
    packInputs.emplace_back(makePictureSummaryPackEntry(
      summaryPic,
      summaryPic,
      buildSummaryPictureEntryName(dirPath, summaryPic)
    ));
  }

  for (auto const& picPath: scannedPics) {
    auto const plannedNameIt = plannedEntryNames.find(picPath);
    auto const entryName = plannedNameIt != plannedEntryNames.end()
      ? plannedNameIt->second
      : picPath.filename().generic_string();
    packInputs.emplace_back(makePictureRegularPackEntry(picPath, picPath, entryName));
  }

  return packInputs;
}

auto buildCompressedPicturePackEntryInputs(
  appctx::AppConfig const& config,
  fs::path const& dirPath,
  std::span<fs::path const> scannedPics,
  PictureEntryPlan const& plannedEntryNames,
  std::unordered_map<std::string, fs::path> const& compressedByTaskKey
) -> std::vector<pack::detail::PackEntryInput> {
  auto summaryPics = std::vector<fs::path>{};
  if (config.pictureFolderSummary) {
    summaryPics = collectFolderSummaryPictures(dirPath, scannedPics);
  }

  auto packInputs = std::vector<pack::detail::PackEntryInput>{};
  packInputs.reserve(compressedByTaskKey.size());

  for (auto const& summaryPic: summaryPics) {
    auto const entryName =
      toJpgEntryName(buildSummaryPictureEntryName(dirPath, summaryPic));
    auto const compressedIt =
      compressedByTaskKey.find(buildCompressTaskKey(summaryPic, entryName));
    if (compressedIt == compressedByTaskKey.end()) { continue; }

    packInputs.emplace_back(
      makePictureSummaryPackEntry(summaryPic, compressedIt->second, entryName)
    );
  }

  for (auto const& picPath: scannedPics) {
    auto const plannedIt = plannedEntryNames.find(picPath);
    auto const entryName = plannedIt != plannedEntryNames.end()
      ? toJpgEntryName(plannedIt->second)
      : toJpgEntryName(picPath.filename().generic_string());
    auto const compressedIt =
      compressedByTaskKey.find(buildCompressTaskKey(picPath, entryName));
    if (compressedIt == compressedByTaskKey.end()) { continue; }

    packInputs.emplace_back(
      makePictureRegularPackEntry(picPath, compressedIt->second, entryName)
    );
  }

  return packInputs;
}

auto buildPicturePackBaseName(
  std::string const& baseName,
  std::size_t partIndex,
  std::size_t subPartIndex,
  std::size_t totalSubParts
) -> std::string {
  if (baseName.empty()) {
    if (totalSubParts <= 1) { return std::format("part{}.zip", partIndex); }
    return std::format("part{}.{}.zip", partIndex, subPartIndex + 1);
  }

  if (totalSubParts <= 1) { return std::format("{}_part{}.zip", baseName, partIndex); }

  return std::format("{}_part{}.{}.zip", baseName, partIndex, subPartIndex + 1);
}

struct PicturePackNamingState {
  std::string baseName;
  std::vector<pack::FileOrdinalRange> ordinalRanges;
  std::vector<std::pair<std::size_t, std::size_t>> groupNameParts;
  std::vector<std::size_t> subPartCountsByPart;

  auto zipNameFor(std::size_t index) const -> std::string {
    auto const [partIndex, subPartIndex] = groupNameParts.at(index);
    return pack::internal::appendOrdinalRangeSuffix(
      buildPicturePackBaseName(
        baseName,
        partIndex,
        subPartIndex,
        subPartCountsByPart.at(partIndex - 1)
      ),
      ordinalRanges.at(index)
    );
  }
};

struct PictureLogicalBucket {
  fs::path sourceDir;
  std::string sourceDirKey;
  std::optional<pack::detail::PackEntryInput> summaryEntry;
  std::vector<pack::detail::PackEntryInput> regularEntries;
};

auto isSummaryPicturePackEntry(pack::detail::PackEntryInput const& input) -> bool {
  return input.isSummary;
}

auto sortPictureLogicalBucketEntries(PictureLogicalBucket& bucket) -> void {
  std::ranges::sort(
    bucket.regularEntries,
    [](pack::detail::PackEntryInput const& lhs, pack::detail::PackEntryInput const& rhs) {
      auto const lhsKey =
        lhs.fileKey.value_or(naming::stablePathString(lhs.entry.sourcePath));
      auto const rhsKey =
        rhs.fileKey.value_or(naming::stablePathString(rhs.entry.sourcePath));
      return lhsKey < rhsKey;
    }
  );
}

auto logicalEntryCount(PictureLogicalBucket const& bucket) -> std::size_t {
  return bucket.regularEntries.size() + (bucket.summaryEntry.has_value() ? 1 : 0);
}

auto buildPictureLogicalBuckets(
  std::vector<pack::detail::PackEntryInput> const& packInputs
) -> std::vector<PictureLogicalBucket> {
  auto bucketsByDir = std::unordered_map<std::string, PictureLogicalBucket>{};
  bucketsByDir.reserve(packInputs.size());

  for (auto const& input: packInputs) {
    auto const dirKey = naming::stablePathString(input.sourceDir);
    auto [bucketIt, _] = bucketsByDir.try_emplace(
      dirKey,
      PictureLogicalBucket{
        .sourceDir = input.sourceDir,
        .sourceDirKey = dirKey,
      }
    );

    if (isSummaryPicturePackEntry(input)) {
      bucketIt->second.summaryEntry = input;
      continue;
    }

    bucketIt->second.regularEntries.push_back(input);
  }

  auto buckets = std::vector<PictureLogicalBucket>{};
  buckets.reserve(bucketsByDir.size());
  for (auto& [_, bucket]: bucketsByDir) {
    sortPictureLogicalBucketEntries(bucket);
    buckets.push_back(std::move(bucket));
  }

  std::ranges::sort(
    buckets,
    [](PictureLogicalBucket const& lhs, PictureLogicalBucket const& rhs) {
      return lhs.sourceDirKey < rhs.sourceDirKey;
    }
  );

  return buckets;
}

auto buildPictureLogicalParts(std::vector<PictureLogicalBucket> const& buckets)
  -> eh::Result<std::vector<std::vector<pack::detail::PackEntryInput>>> {
  auto logicalParts = std::vector<std::vector<pack::detail::PackEntryInput>>{};
  auto currentPart = std::vector<pack::detail::PackEntryInput>{};
  auto currentCount = std::size_t{0};

  for (auto const& bucket: buckets) {
    auto const bucketCount = logicalEntryCount(bucket);
    if (bucketCount > kMaxPicturesPerPack) {
      return eh::makeError(
        "Directory '{}' requires {} entries including summary, exceeding the {}-picture "
        "logical pack limit.",
        bucket.sourceDir.string(),
        bucketCount,
        kMaxPicturesPerPack
      );
    }

    if (!currentPart.empty() && currentCount + bucketCount > kMaxPicturesPerPack) {
      logicalParts.push_back(std::move(currentPart));
      currentPart = {};
      currentCount = 0;
    }

    if (bucket.summaryEntry.has_value()) {
      currentPart.push_back(bucket.summaryEntry.value());
    }
    currentPart.insert(
      currentPart.end(),
      bucket.regularEntries.begin(),
      bucket.regularEntries.end()
    );
    currentCount += bucketCount;
  }

  if (!currentPart.empty()) { logicalParts.push_back(std::move(currentPart)); }
  return logicalParts;
}

auto validateSummaryEntriesFitFirstPhysicalPack(
  std::span<pack::detail::PackEntryInput const> logicalPart,
  std::size_t partIndex
) -> eh::Result<void> {
  auto summarySize = std::uintmax_t{0};
  for (auto const& input: logicalPart) {
    if (!isSummaryPicturePackEntry(input)) { continue; }
    summarySize += fs::file_size(input.entry.sourcePath);
  }

  if (summarySize > pack::kDefaultMaxArchiveGroupSize) {
    return eh::makeError(
      "Summary entries in logical pack {} exceed the single-archive size limit.",
      partIndex + 1
    );
  }

  return {};
}

auto buildPicturePackPlan(
  appctx::AppConfig const& config,
  fs::path const& dirPath,
  fs::path const& zipFileDir,
  std::string baseName,
  std::vector<pack::detail::PackEntryInput> const& packInputs
) -> eh::Result<pack::PackPlan> {
  if (packInputs.empty()) {
    return eh::makeError("No pictures found to pack in directory: {}", dirPath.string());
  }

  auto const logicalPartsRes =
    buildPictureLogicalParts(buildPictureLogicalBuckets(packInputs));
  if (!logicalPartsRes) { return eh::makeError("{}", logicalPartsRes.error()); }

  auto groupedPics = std::vector<std::vector<pack::PackFileEntry>>{};
  auto groupNameParts = std::vector<std::pair<std::size_t, std::size_t>>{};
  auto subPartCountsByPart = std::vector<std::size_t>{};
  pack::Packer packer;

  for (auto partIndex = std::size_t{0}; partIndex < logicalPartsRes->size();
       ++partIndex) {
    auto const summaryFitRes = validateSummaryEntriesFitFirstPhysicalPack(
      logicalPartsRes->at(partIndex),
      partIndex
    );
    if (!summaryFitRes) { return eh::makeError("{}", summaryFitRes.error()); }

    auto physicalGroups = packer.groupPackEntries(
      logicalPartsRes->at(partIndex),
      pack::kDefaultMaxArchiveGroupSize,
      std::nullopt,
      std::optional<std::size_t>{0}
    );

    if (subPartCountsByPart.size() < partIndex + 1) {
      subPartCountsByPart.resize(partIndex + 1, 0);
    }

    for (auto subPartIndex = std::size_t{0}; subPartIndex < physicalGroups.size();
         ++subPartIndex) {
      groupedPics.emplace_back(std::move(physicalGroups[subPartIndex]));
      groupNameParts.emplace_back(partIndex + 1, subPartIndex);
      ++subPartCountsByPart[partIndex];
    }
  }

  auto const ordinalRanges = pack::internal::buildGroupOrdinalRanges(groupedPics);
  auto picturePackNaming = PicturePackNamingState{
    .baseName = std::move(baseName),
    .ordinalRanges = ordinalRanges,
    .groupNameParts = groupNameParts,
    .subPartCountsByPart = subPartCountsByPart,
  };
  auto const picturePackNamingState =
    std::make_shared<PicturePackNamingState>(std::move(picturePackNaming));

  return pack::PackPlan{
    .groups = std::move(groupedPics),
    .outputDir = zipFileDir,
    .zipNameForIndex =  //
    [picturePackNamingState](std::size_t index) {
      return picturePackNamingState->zipNameFor(index);
    },
    .maxParallelJobs = config.maxParallelJobs,
    .removeOnFailure = true,
    .compact = !config.fullProgress,
  };
}

auto addCompressTask(
  fs::path const& tempDir,
  std::error_code& ec,
  std::vector<CompressTask>& compressTasks,
  fs::path const& picPath,
  std::string const& entryName
) -> void {
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
}

auto confirmPicturePack(appctx::AppConfig const& config) -> bool {
  return readUserIpt(
    config.yesToAll,
    "do you want to proceed with packing the pictures? (Y/n): "
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

    auto compressTasks = std::vector<CompressTask>{};
    compressTasks.reserve(scannedPics.size() + summaryPics.size());

    for (auto const& summaryPic: summaryPics) {
      auto const entryName = buildSummaryPictureEntryName(dirPath, summaryPic);
      addCompressTask(tempDir, ec, compressTasks, summaryPic, entryName);
    }

    for (auto const& picPath: scannedPics) {
      auto const plannedIt = plannedEntryNames.find(picPath);
      auto const entryName = plannedIt != plannedEntryNames.end()
        ? plannedIt->second
        : picPath.filename().generic_string();
      addCompressTask(tempDir, ec, compressTasks, picPath, entryName);
    }

    terminal::println(
      Info,
      "Compressing {} picture(s) to JPEG (quality={})...",
      terminal::count(compressTasks.size()),
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

    auto compressedByTaskKey = std::unordered_map<std::string, fs::path>{};
    compressedByTaskKey.reserve(compressResults.size());
    for (auto const& result: compressResults) {
      compressedByTaskKey.emplace(
        buildCompressTaskKey(result.originalPath, result.entryName),
        result.compressedPath
      );
    }

    auto const packInputs = buildCompressedPicturePackEntryInputs(
      ctx.config,
      dirPath,
      scannedPics,
      plannedEntryNames,
      compressedByTaskKey
    );
    if (packInputs.empty()) {
      fs::remove_all(tempDir, ec);
      return eh::makeError("No compressed pictures available to pack.");
    }

    auto const planRes =
      buildPicturePackPlan(ctx.config, dirPath, outputDir, std::string{}, packInputs);
    if (!planRes) {
      fs::remove_all(tempDir, ec);
      return eh::makeError("Failed to pack pictures: {}", planRes.error());
    }

    terminal::println(
      Info,
      "Packing {} compressed picture entry(s) into archives...",
      terminal::count(packInputs.size())
    );

    auto const packRes = pack::execute(*planRes, ctx.runtime.jobState.get());

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

  // --- Non-compress path ---
  auto const scannedPics = readAllPics(ctx.config, dirPath);
  if (scannedPics.empty()) {
    return eh::makeError("No pictures found in directory: {}", dirPath.string());
  }

  terminal::println(
    Info,
    "Picture scan completed, {} picture(s) found, grouping into "
    "package batch(es).",
    terminal::count(scannedPics.size())
  );

  if (!confirmPicturePack(ctx.config)) {
    terminal::println(Warning, "Packing task canceled by user.");
    return 0;
  }

  auto const plannedEntryNames =
    planPictureZipEntryNames(ctx.config, dirPath, scannedPics);

  auto packInputs =
    buildPicturePackEntryInputs(ctx.config, dirPath, scannedPics, plannedEntryNames);

  auto const planRes = buildPicturePackPlan(
    ctx.config,
    dirPath,
    outputDir,
    dirPath.filename().string(),
    packInputs
  );
  if (!planRes) { return eh::makeError("Failed to pack pictures: {}", planRes.error()); }

  auto const packRes = pack::execute(*planRes, ctx.runtime.jobState.get());

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
  auto const scannedPics = readAllPics(config, dirPath);
  if (scannedPics.empty()) {
    return eh::makeError("No pictures found in directory: {}", dirPath.string());
  }

  if (!confirmPicturePack(config)) {
    terminal::println(Warning, "Packing task canceled by user.");
    return eh::makeError("Packing task canceled by user.");
  }

  auto const plannedEntryNames = planPictureZipEntryNames(config, dirPath, scannedPics);

  auto const packInputs =
    buildPicturePackEntryInputs(config, dirPath, scannedPics, plannedEntryNames);
  auto const planRes = buildPicturePackPlan(
    config,
    dirPath,
    zipFileDir,
    dirPath.filename().string(),
    packInputs
  );
  if (!planRes) {
    auto const errMsg = std::format(
      "Failed to pack pictures in {}: {}",
      zipFileDir.string(),
      planRes.error()
    );
    spdlog::error(errMsg);
    return eh::makeError("{}", errMsg);
  }

  auto const packRes = pack::execute(*planRes);

  if (!packRes) {
    auto const errMsg = std::format(
      "Failed to pack pictures in {}: {}",
      zipFileDir.string(),
      packRes.error()
    );
    spdlog::error(errMsg);
    return eh::makeError("{}", errMsg);
  }
  if (packRes->exitCode != 0) {
    return eh::makeError("Packing failed with exit code: {}", packRes->exitCode);
  }

  return {};
}
