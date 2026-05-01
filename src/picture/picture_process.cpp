#include "picture/picture_process.h"

#include "picture/picture_compress.h"

#include "core/collision_naming.h"
#include "core/media_scanner.h"
#include "infra/terminal.h"
#include "pack/pack.h"
#include "pack/packer_types.h"
#include "utils/utils.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;
using namespace std::literals;
namespace naming = collisionnaming;

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
) -> std::vector<pack::detail::PackEntryInput> {
  auto summaryPics = std::vector<fs::path>{};
  if (config.pictureFolderSummary) {
    summaryPics = collectFolderSummaryPictures(dirPath, scannedPics);
  }

  auto packInputs = std::vector<pack::detail::PackEntryInput>{};
  packInputs.reserve(scannedPics.size() + summaryPics.size());

  for (auto const& summaryPic: summaryPics) {
    auto const dirKey = naming::stablePathString(summaryPic.parent_path());
    packInputs.emplace_back(
      pack::detail::PackEntryInput{
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
      pack::detail::PackEntryInput{
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

    auto compressedPaths = std::vector<fs::path>{};
    compressedPaths.reserve(compressResults.size());

    for (auto const& summaryPic: summaryPics) {
      auto const compressedIt = compressedSet.find(summaryPic);
      if (compressedIt == compressedSet.end()) { continue; }
      compressedPaths.push_back(compressedIt->second);
    }

    for (auto const& picPath: scannedPics) {
      auto const compressedIt = compressedSet.find(picPath);
      if (compressedIt == compressedSet.end()) { continue; }
      compressedPaths.push_back(compressedIt->second);
    }

    terminal::println(
      Info,
      "Packing {} compressed picture(s) into archives...",
      terminal::count(compressedPaths.size())
    );

    auto const packRes = pack::execute(
      pack::PackRequest{
        .entries = std::move(compressedPaths),
        .mode = pack::PackMode::Media,
        .outputDir = outputDir,
        .compact = !ctx.config.fullProgress,
        .removeOnFailure = true,
        .maxParallelJobs = ctx.config.maxParallelJobs,
        .jobState = ctx.runtime.jobState.get(),
      }
    );

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

  auto entryNameForFile = [plannedEntryNames =
                             std::move(plannedEntryNames)](fs::path const& filePath) {
    auto const it = plannedEntryNames.find(filePath);
    return it != plannedEntryNames.end() ? it->second
                                         : filePath.filename().generic_string();
  };

  auto const packInputs =
    buildPicturePackEntryInputs(ctx.config, dirPath, scannedPics, plannedEntryNames);

  auto sortedEntries = std::vector<fs::path>{};
  sortedEntries.reserve(packInputs.size());
  for (auto const& input: packInputs) { sortedEntries.push_back(input.entry.sourcePath); }

  auto const packRes = pack::execute(
    pack::PackRequest{
      .entries = std::move(sortedEntries),
      .mode = pack::PackMode::Media,
      .outputDir = outputDir,
      .compact = !ctx.config.fullProgress,
      .removeOnFailure = true,
      .naming =
        pack::NamingConfig{
          .layout = ctx.config.outputLayout,
          .forceConflictHandling = shouldForcePictureConflictNaming(ctx.config),
          .baseName = dirPath.filename().string(),
        },
      .maxParallelJobs = ctx.config.maxParallelJobs,
      .jobState = ctx.runtime.jobState.get(),
      .entryNameForFile = std::move(entryNameForFile),
    }
  );

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

  auto entryNameForFile = [plannedEntryNames =
                             std::move(plannedEntryNames)](fs::path const& filePath) {
    auto const it = plannedEntryNames.find(filePath);
    return it != plannedEntryNames.end() ? it->second
                                         : filePath.filename().generic_string();
  };

  auto const packInputs =
    buildPicturePackEntryInputs(config, dirPath, scannedPics, plannedEntryNames);

  auto sortedEntries = std::vector<fs::path>{};
  sortedEntries.reserve(packInputs.size());
  for (auto const& input: packInputs) { sortedEntries.push_back(input.entry.sourcePath); }

  auto const packRes = pack::execute(
    pack::PackRequest{
      .entries = std::move(sortedEntries),
      .mode = pack::PackMode::Media,
      .outputDir = zipFileDir,
      .compact = !config.fullProgress,
      .removeOnFailure = true,
      .naming =
        pack::NamingConfig{
          .layout = config.outputLayout,
          .forceConflictHandling = shouldForcePictureConflictNaming(config),
          .baseName = dirPath.filename().string(),
        },
      .maxParallelJobs = config.maxParallelJobs,
      .jobState = nullptr,
      .entryNameForFile = std::move(entryNameForFile),
    }
  );

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
