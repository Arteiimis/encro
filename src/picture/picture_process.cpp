#include "picture/picture_process.h"

#include "picture/picture_compress.h"

#include "core/collision_naming.h"
#include "core/media_scanner.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "pack/pack.h"
#include "utils/utils.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
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

auto canceledExitCodeForPromptAbort() -> int {
  return stopsignal::isStopRequested() ? stopsignal::kCanceledExitCode : 0;
}

// --- Extracted helper functions ---

auto buildCompressedResultLookup(std::vector<CompressResult> const& compressResults)
  -> std::unordered_map<std::string, fs::path> {
  auto lookup = std::unordered_map<std::string, fs::path>{};
  lookup.reserve(compressResults.size());
  for (auto const& result: compressResults) {
    lookup.emplace(
      buildCompressTaskKey(result.originalPath, result.entryName),
      result.compressedPath
    );
  }
  return lookup;
}

auto buildPicturePackRequest(
  std::vector<pack::PackEntryInput>&& packInputs,
  fs::path const& outputDir,
  appctx::AppContext const& ctx,
  std::string const& baseName
) -> pack::PackRequest {
  return pack::PackRequest{
    .entryInputs = std::move(packInputs),
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .compact = !ctx.config.fullProgress,
    .removeOnFailure = true,
    .naming =
      pack::NamingConfig{
        .namingStrategy = pack::NamingStrategy::Flat,
        .baseName = baseName,
      },
    .groupingStrategy = pack::GroupingStrategy::PerSourceDirKeepTogether,
    .maxParallelJobs = ctx.config.maxParallelJobs,
    .jobState = ctx.runtime.jobState.get(),
  };
}

using SourceResolver = std::function<std::optional<
  std::pair<fs::path, std::string>
>(fs::path const& picPath, std::string const& entryName)>;

auto buildPackEntryInputs(
  std::vector<fs::path> const& summaryPics,
  std::vector<fs::path> const& scannedPics,
  PictureEntryPlan const& plannedEntryNames,
  fs::path const& dirPath,
  SourceResolver const& resolveSource,
  std::function<std::string(std::string const&)> const& entryNameTransform
) -> std::vector<pack::PackEntryInput> {
  auto packInputs = std::vector<pack::PackEntryInput>{};
  packInputs.reserve(scannedPics.size() + summaryPics.size());

  for (auto const& summaryPic: summaryPics) {
    auto const rawEntryName = buildSummaryPictureEntryName(dirPath, summaryPic);
    auto const entryName = entryNameTransform(rawEntryName);
    auto const resolved = resolveSource(summaryPic, entryName);
    if (!resolved) { continue; }
    packInputs.emplace_back(
      pack::PackEntryInput{
        .entry =
          pack::PackFileEntry{
            .sourcePath = resolved->first,
            .zipEntryName = resolved->second,
            .isSummary = true,
          },
        .sourceDir = summaryPic.parent_path(),
        .sourceKey = naming::stablePathString(summaryPic.parent_path()),
        .fileKey = naming::stablePathString(summaryPic),
        .isSummary = true,
      }
    );
  }

  for (auto const& picPath: scannedPics) {
    auto const plannedIt = plannedEntryNames.find(picPath);
    auto const rawEntryName = plannedIt != plannedEntryNames.end()
      ? plannedIt->second
      : picPath.filename().generic_string();
    auto const entryName = entryNameTransform(rawEntryName);
    auto const resolved = resolveSource(picPath, entryName);
    if (!resolved) { continue; }
    packInputs.emplace_back(
      pack::PackEntryInput{
        .entry =
          pack::PackFileEntry{
            .sourcePath = resolved->first,
            .zipEntryName = resolved->second,
          },
        .sourceDir = picPath.parent_path(),
        .sourceKey = naming::stablePathString(picPath.parent_path()),
        .fileKey = naming::stablePathString(picPath),
      }
    );
  }

  return packInputs;
}

auto executeDirectPackWorkflow(
  appctx::AppContext& ctx,
  fs::path const& dirPath,
  fs::path const& outputDir
) -> eh::Result<int> {
  auto const scannedPics = readAllPics(ctx.config, dirPath);
  if (scannedPics.empty()) {
    return eh::makeError("No pictures found in directory: {}", dirPath.string());
  }

  terminal::println(
    Info,
    "Picture scan completed, {} picture(s) found, grouping into package batch(es).",
    terminal::count(scannedPics.size())
  );

  if (!confirmPicturePack(ctx.config)) {
    terminal::println(Warning, "Packing task canceled by user.");
    return canceledExitCodeForPromptAbort();
  }

  auto const plannedEntryNames =
    planPictureZipEntryNames(ctx.config, dirPath, scannedPics);

  auto summaryPics = std::vector<fs::path>{};
  if (ctx.config.pictureFolderSummary) {
    summaryPics = collectFolderSummaryPictures(dirPath, scannedPics);
  }

  auto const resolveSource = [](fs::path const& picPath, std::string const& entryName)
    -> std::optional<std::pair<fs::path, std::string>> {
    return std::pair{picPath, entryName};
  };
  auto const identityTransform = [](std::string const& s) -> std::string { return s; };

  auto packInputs = buildPackEntryInputs(
    summaryPics,
    scannedPics,
    plannedEntryNames,
    dirPath,
    resolveSource,
    identityTransform
  );

  auto const request = buildPicturePackRequest(
    std::move(packInputs),
    outputDir,
    ctx,
    dirPath.filename().string()
  );

  auto const packRes = pack::execute(request);
  if (!packRes) { return eh::makeError("Failed to pack pictures: {}", packRes.error()); }
  if (packRes->exitCode != 0) { return packRes->exitCode; }

  terminal::println(
    Success,
    "All pictures packed successfully to: {}",
    terminal::path(outputDir)
  );
  return 0;
}

auto executeCompressPackWorkflow(
  appctx::AppContext& ctx,
  fs::path const& dirPath,
  fs::path const& outputDir
) -> eh::Result<int> {
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
    return canceledExitCodeForPromptAbort();
  }

  auto const tempDir = outputDir / ".compress_tmp";
  auto ec = std::error_code{};
  fs::remove_all(tempDir, ec);
  fs::create_directories(tempDir);

  auto summaryPics = std::vector<fs::path>{};
  if (ctx.config.pictureFolderSummary) {
    summaryPics = collectFolderSummaryPictures(dirPath, scannedPics);
  }

  auto const plannedEntryNames =
    planPictureZipEntryNames(ctx.config, dirPath, scannedPics);

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

  if (stopsignal::isStopRequested()) {
    fs::remove_all(tempDir, ec);
    terminal::println(Warning, "Compression task canceled by user.");
    return stopsignal::kCanceledExitCode;
  }

  if (compressResults.empty()) {
    fs::remove_all(tempDir, ec);
    return eh::makeError("All picture compressions failed.");
  }

  terminal::println(
    Info,
    "{} picture(s) compressed, preparing pack plan...",
    terminal::count(compressResults.size())
  );

  auto const compressedByTaskKey = buildCompressedResultLookup(compressResults);

  auto const resolveSource =
    [&compressedByTaskKey](fs::path const& picPath, std::string const& entryName)
    -> std::optional<std::pair<fs::path, std::string>> {
    auto const taskKey = buildCompressTaskKey(picPath, entryName);
    auto const it = compressedByTaskKey.find(taskKey);
    if (it == compressedByTaskKey.end()) { return std::nullopt; }
    return std::pair{it->second, entryName};
  };

  auto packInputs = buildPackEntryInputs(
    summaryPics,
    scannedPics,
    plannedEntryNames,
    dirPath,
    resolveSource,
    toJpgEntryName
  );

  if (packInputs.empty()) {
    fs::remove_all(tempDir, ec);
    return eh::makeError("No compressed pictures available to pack.");
  }

  terminal::println(
    Info,
    "Packing {} compressed picture entry(s) into archives...",
    terminal::count(packInputs.size())
  );

  auto const request =
    buildPicturePackRequest(std::move(packInputs), outputDir, ctx, std::string{});

  auto const packRes = pack::execute(request);
  fs::remove_all(tempDir, ec);

  if (!packRes) { return eh::makeError("Failed to pack pictures: {}", packRes.error()); }
  if (packRes->exitCode != 0) { return packRes->exitCode; }

  terminal::println(
    Success,
    "All pictures packed successfully to: {}",
    terminal::path(outputDir)
  );
  return 0;
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
    return executeCompressPackWorkflow(ctx, dirPath, outputDir);
  }
  return executeDirectPackWorkflow(ctx, dirPath, outputDir);
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

  auto summaryPics = std::vector<fs::path>{};
  if (config.pictureFolderSummary) {
    summaryPics = collectFolderSummaryPictures(dirPath, scannedPics);
  }

  auto packInputs = std::vector<pack::PackEntryInput>{};
  packInputs.reserve(scannedPics.size() + summaryPics.size());

  for (auto const& summaryPic: summaryPics) {
    packInputs.push_back({
      .entry =
        pack::PackFileEntry{
          .sourcePath = summaryPic,
          .zipEntryName = buildSummaryPictureEntryName(dirPath, summaryPic),
          .isSummary = true,
        },
      .sourceDir = summaryPic.parent_path(),
      .sourceKey = naming::stablePathString(summaryPic.parent_path()),
      .fileKey = naming::stablePathString(summaryPic),
      .isSummary = true,
    });
  }

  for (auto const& picPath: scannedPics) {
    auto const plannedNameIt = plannedEntryNames.find(picPath);
    auto const entryName = plannedNameIt != plannedEntryNames.end()
      ? plannedNameIt->second
      : picPath.filename().generic_string();
    packInputs.push_back({
      .entry =
        pack::PackFileEntry{
          .sourcePath = picPath,
          .zipEntryName = entryName,
        },
      .sourceDir = picPath.parent_path(),
      .sourceKey = naming::stablePathString(picPath.parent_path()),
      .fileKey = naming::stablePathString(picPath),
    });
  }

  auto request = pack::PackRequest{
    .entryInputs = std::move(packInputs),
    .mode = pack::PackMode::Media,
    .outputDir = zipFileDir,
    .compact = !config.fullProgress,
    .removeOnFailure = true,
    .naming =
      pack::NamingConfig{
        .namingStrategy = pack::NamingStrategy::Flat,
        .baseName = dirPath.filename().string(),
      },
    .groupingStrategy = pack::GroupingStrategy::PerSourceDirKeepTogether,
    .maxParallelJobs = config.maxParallelJobs,
  };
  auto const packRes = pack::execute(request);

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
