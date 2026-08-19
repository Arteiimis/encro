#include "picture/picture_process.h"

#include "picture/picture_compress.h"

#include "core/collision_naming.h"
#include "core/job_state.h"
#include "core/media_scanner.h"
#include "core/work_dirs.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "pack/pack.h"
#include "utils/utils.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <unordered_map>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::PICTURE_PROCESS);

namespace fs = std::filesystem;
using namespace std::literals;
namespace naming = collisionnaming;

using enum terminal::MessageKind;

namespace {

using PictureEntryPlan = std::unordered_map<fs::path, std::string>;
using stopsignal::canceledExitCodeForPromptAbort;

constexpr auto kMaxPicturesPerPack = std::size_t{2000};
constexpr auto kPictureArchiveBaseName = std::string_view{"pics"};
constexpr auto kDefaultPictureCompressQuality = 2;

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

auto addCompressTask(
  fs::path const& tempDir,
  std::error_code& ec,
  std::vector<CompressTask>& compressTasks,
  fs::path const& picPath,
  std::string const& entryName
) -> void {
  auto const jpgEntryName = toJpgEntryName(entryName);
  auto const outputPath = tempDir / jpgEntryName;

  auto outEc = std::error_code{};
  auto const outputTime = fs::last_write_time(outputPath, outEc);
  auto srcEc = std::error_code{};
  auto const sourceTime = fs::last_write_time(picPath, srcEc);
  if (!outEc && !srcEc && outputTime >= sourceTime) { return; }

  fs::create_directories(outputPath.parent_path(), ec);

  compressTasks.push_back(
    CompressTask{
      .inputPath = picPath,
      .outputPath = outputPath,
      .entryName = jpgEntryName,
      .originalEntryName = entryName,
    }
  );
}

auto confirmPicturePack(appctx::AppConfig const& config) -> bool {
  return readUserIpt(
    config.yesToAll,
    "do you want to proceed with packing the pictures? (Y/n): "
  );
}

// --- Extracted helper functions ---

auto buildPicturePackRequest(
  std::vector<pack::PackEntryInput>&& packInputs,
  fs::path const& outputDir,
  appctx::AppContext const& ctx
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
        .baseName = std::string{kPictureArchiveBaseName},
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
  auto const scannedPics = [&]() {
    logging::ScopedTimer timer("picture.scan");
    auto const scanPathStr = dirPath.string();
    logging::ScopedErrorContext scopedCtx("picture.scan", scanPathStr);
    return readAllPics(ctx.config, dirPath);
  }();
  if (!scannedPics) { return eh::makeError("{}", scannedPics.error()); }
  if (scannedPics->empty()) {
    return eh::makeError("No pictures found in directory: {}", dirPath.string());
  }
  auto const& pics = scannedPics.value();

  terminal::println(
    Info,
    "Picture scan completed, {} picture(s) found, grouping into package batch(es).",
    terminal::count(pics.size())
  );

  if (!confirmPicturePack(ctx.config)) {
    terminal::println(Warning, "Packing task canceled by user.");
    return canceledExitCodeForPromptAbort();
  }

  auto const plannedEntryNames = planPictureZipEntryNames(ctx.config, dirPath, pics);

  auto summaryPics = std::vector<fs::path>{};
  if (ctx.config.pictureFolderSummary) {
    summaryPics = collectFolderSummaryPictures(dirPath, pics);
  }

  auto const resolveSource = [](fs::path const& picPath, std::string const& entryName)
    -> std::optional<std::pair<fs::path, std::string>> {
    return std::pair{picPath, entryName};
  };
  auto const identityTransform = [](std::string const& s) -> std::string { return s; };

  auto packInputs = buildPackEntryInputs(
    summaryPics,
    pics,
    plannedEntryNames,
    dirPath,
    resolveSource,
    identityTransform
  );

  auto const request = buildPicturePackRequest(std::move(packInputs), outputDir, ctx);

  auto const packRes = [&]() {
    logging::ScopedTimer timer("picture.pack");
    auto const packLabel = std::format("{} picture(s)", pics.size());
    logging::ScopedErrorContext scopedCtx("picture.pack", packLabel);
    return pack::execute(request);
  }();
  if (!packRes) { return eh::makeError("Failed to pack pictures: {}", packRes.error()); }
  if (packRes->exitCode != 0) { return packRes->exitCode; }

  terminal::println(
    Success,
    "All pictures packed successfully to: {}",
    terminal::path(outputDir)
  );
  return 0;
}

auto scanPictures(appctx::AppContext& ctx, fs::path const& dirPath)
  -> eh::Result<std::vector<fs::path>> {
  auto const scannedPics = [&]() {
    logging::ScopedTimer timer("picture.scan");
    auto const scanPathStr = dirPath.string();
    logging::ScopedErrorContext scopedCtx("picture.scan", scanPathStr);
    return readAllPics(ctx.config, dirPath);
  }();
  if (!scannedPics) { return eh::makeError("{}", scannedPics.error()); }
  if (scannedPics->empty()) {
    return eh::makeError("No pictures found in directory: {}", dirPath.string());
  }
  return scannedPics.value();
}

// Preps the cache directory: clears stale compress_* siblings (other
// qualities) from the hidden .encro dir and one-time-removes legacy
// .compress_tmp* dirs from the packed output dir, then rebuilds the current
// temp dir unless the resume cache matches. Never clears on a stop-requested
// resume (cache must survive).
auto prepareCompressTempDir(
  fs::path const& outputDir,
  fs::path const& workRoot,
  fs::path const& tempDir,
  bool jobStateMatched
) -> void {
  auto ec = std::error_code{};
  auto const keepCache = jobStateMatched && fs::exists(tempDir, ec) && !ec;
  auto removeSiblings = [&](fs::path const& parent, std::string_view prefix) {
    if (!fs::is_directory(parent, ec) || ec) { return; }
    for (auto const& de: fs::directory_iterator(parent)) {
      auto deEc = std::error_code{};
      if (!de.is_directory(deEc) || deEc) { continue; }
      if (de.path().filename().string().starts_with(prefix) && de.path() != tempDir) {
        fs::remove_all(de.path(), deEc);
      }
    }
  };
  // New layout: sibling quality caches under the hidden .encro dir.
  removeSiblings(workdirs::encroDir(workRoot), "compress_");
  // Legacy layout: .compress_tmp* caches inside the old packed output dir
  // (historical location) are removed once; the new cache never lives there.
  removeSiblings(outputDir, ".compress_tmp");
  if (!keepCache) {
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir);
    workdirs::setHiddenOnEncroDir(tempDir);
  }
}

auto buildCompressTaskList(
  fs::path const& tempDir,
  std::vector<fs::path> const& summaryPics,
  std::vector<fs::path> const& pics,
  std::unordered_map<fs::path, std::string> const& plannedEntryNames,
  fs::path const& dirPath
) -> std::vector<CompressTask> {
  auto compressTasks = std::vector<CompressTask>{};
  compressTasks.reserve(pics.size() + summaryPics.size());
  auto ec = std::error_code{};

  for (auto const& summaryPic: summaryPics) {
    auto const entryName = buildSummaryPictureEntryName(dirPath, summaryPic);
    addCompressTask(tempDir, ec, compressTasks, summaryPic, entryName);
  }

  for (auto const& picPath: pics) {
    auto const plannedIt = plannedEntryNames.find(picPath);
    auto const entryName = plannedIt != plannedEntryNames.end()
      ? plannedIt->second
      : picPath.filename().generic_string();
    addCompressTask(tempDir, ec, compressTasks, picPath, entryName);
  }
  return compressTasks;
}

struct CompressPhaseOutcome {
  bool canceled;
  std::vector<CompressResult> results;
};

// Runs the JPEG batch; returns the results on success, the canceled exit code
// on user stop, or an error when every picture failed.
auto runCompressionPhase(
  appctx::AppContext& ctx,
  std::vector<CompressTask> const& compressTasks,
  int quality,
  std::size_t maxParallel
) -> eh::Result<CompressPhaseOutcome> {
  if (auto* store = ctx.runtime.jobState.get(); store != nullptr) {
    auto const phaseTask = std::vector{jobstate::makeCompressPhaseTask()};
    store->mergeTasks(phaseTask);
    store->markRunning(jobstate::kCompressPhaseTaskId);
  }

  auto const compressResults = [&]() {
    logging::ScopedTimer timer("picture.compress");
    auto const compressLabel =
      std::format("{} picture(s) q={}", compressTasks.size(), quality);
    logging::ScopedErrorContext scopedCtx("picture.compress", compressLabel);
    return compressImageBatch(ctx, compressTasks, quality, maxParallel);
  }();

  if (stopsignal::isStopRequested()) {
    if (auto* store = ctx.runtime.jobState.get(); store != nullptr) {
      store->markInterrupted(jobstate::kCompressPhaseTaskId, "canceled by user");
    }
    terminal::println(Warning, "Compression task canceled by user.");
    return CompressPhaseOutcome{.canceled = true, .results = {}};
  }

  if (compressResults.empty()) {
    if (auto* store = ctx.runtime.jobState.get(); store != nullptr) {
      store
        ->markFailed(jobstate::kCompressPhaseTaskId, "all picture compressions failed");
    }
    return eh::makeError("All picture compressions failed.");
  }

  if (auto* store = ctx.runtime.jobState.get(); store != nullptr) {
    store->markSucceeded(jobstate::kCompressPhaseTaskId);
  }
  return CompressPhaseOutcome{.canceled = false, .results = compressResults};
}

// Prefer the compressed JPEG when it is smaller than the source, else fall
// back to the original so packing never enlarges the archive.
auto resolveCompressedSource(
  fs::path const& picPath,
  std::string const& entryName,
  fs::path const& tempDir
) -> std::optional<std::pair<fs::path, std::string>> {
  auto const jpgEntryName = toJpgEntryName(entryName);
  auto const outputPath = tempDir / jpgEntryName;

  auto outEc = std::error_code{};
  auto const outputSize = fs::file_size(outputPath, outEc);
  if (outEc) { return std::nullopt; }

  auto srcEc = std::error_code{};
  auto const sourceSize = fs::file_size(picPath, srcEc);
  if (srcEc) { return std::nullopt; }

  if (outputSize <= sourceSize) { return std::pair{outputPath, jpgEntryName}; }
  return std::pair{picPath, entryName};
}

// Packs the resolved picture entries and clears the cache (unless the run
// was stopped, so resume can reuse it).
auto executePicturePack(
  fs::path const& outputDir,
  fs::path const& tempDir,
  std::vector<pack::PackEntryInput> packInputs,
  appctx::AppContext& ctx
) -> eh::Result<int> {
  auto ec = std::error_code{};
  terminal::println(
    Info,
    "Packing {} picture entry(s) into archives...",
    terminal::count(packInputs.size())
  );

  auto const packInputCount = packInputs.size();
  auto const request = buildPicturePackRequest(std::move(packInputs), outputDir, ctx);

  auto const packRes = [&]() {
    logging::ScopedTimer timer("picture.pack");
    auto const packLabel = std::format("{} entry(s)", packInputCount);
    logging::ScopedErrorContext scopedCtx("picture.pack", packLabel);
    return pack::execute(request);
  }();
  if (!stopsignal::isStopRequested()) { fs::remove_all(tempDir, ec); }

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
  auto const pics = scanPictures(ctx, dirPath);
  if (!pics) { return eh::makeError("{}", pics.error()); }
  auto const& scannedPics = pics.value();

  auto const quality = ctx.config.imageQuality.value_or(kDefaultPictureCompressQuality);
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

  auto const workRootRes = workdirs::resolveWorkRoot(ctx.config);
  if (!workRootRes) { return eh::makeError("{}", workRootRes.error()); }
  auto const tempDir = workdirs::compressCacheDir(*workRootRes, quality);
  auto ec = std::error_code{};
  fs::create_directories(outputDir, ec);

  prepareCompressTempDir(outputDir, *workRootRes, tempDir, ctx.runtime.jobStateMatched);

  auto summaryPics = std::vector<fs::path>{};
  if (ctx.config.pictureFolderSummary) {
    summaryPics = collectFolderSummaryPictures(dirPath, scannedPics);
  }

  auto const plannedEntryNames =
    planPictureZipEntryNames(ctx.config, dirPath, scannedPics);

  auto const compressTasks =
    buildCompressTaskList(tempDir, summaryPics, scannedPics, plannedEntryNames, dirPath);

  auto const maxParallel = ctx.config.maxParallelJobs.value_or(10);
  if (!compressTasks.empty()) {
    terminal::println(
      Info,
      "Compressing {} picture(s) to JPEG (quality={})...",
      terminal::count(compressTasks.size()),
      terminal::count(quality)
    );

    auto const compressOutcome =
      runCompressionPhase(ctx, compressTasks, quality, maxParallel);
    if (!compressOutcome) {  // all failed
      fs::remove_all(tempDir, ec);
      return eh::makeError("{}", compressOutcome.error());
    }
    if (
      compressOutcome.value().canceled
    ) {  // NOLINT(bugprone-unchecked-optional-access): guarded by the !compressOutcome check above
      return stopsignal::kCanceledExitCode;
    }
    terminal::println(
      Info,
      "{} picture(s) prepared for packing, preparing pack plan...",
      terminal::count(compressOutcome.value().results.size())
    );
  }

  auto packInputs = buildPackEntryInputs(
    summaryPics,
    scannedPics,
    plannedEntryNames,
    dirPath,
    // NOLINTNEXTLINE(bugprone-exception-escape): path ops may throw bad_alloc; nullopt on failure
    [tempDir](fs::path const& picPath, std::string const& entryName) {
      return resolveCompressedSource(picPath, entryName, tempDir);
    },
    [](std::string const& name) -> std::string { return name; }
  );

  if (packInputs.empty()) {
    fs::remove_all(tempDir, ec);
    return eh::makeError("No compressed pictures available to pack.");
  }
  return executePicturePack(outputDir, tempDir, std::move(packInputs), ctx);
}

}  // namespace

auto readAllPics(appctx::AppConfig const& config, fs::path const& dirPath)
  -> eh::Result<std::vector<fs::path>> {
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

  auto const scanRes = media::scanByExtensions(dirPath, pictureTypes, config.recursive);
  if (!scanRes) { return eh::makeError("Failed to scan pictures: {}", scanRes.error()); }
  for (auto const& warning: scanRes->warnings) { LOG_WARN("{}", warning); }
  return scanRes->matches;
}

auto runPicturePackWorkflow(appctx::AppContext& ctx, fs::path const& dirPath)
  -> eh::Result<int> {
  auto const outputDir = ctx.config.outputPath.value_or(dirPath) / "packed";

  if (ctx.config.compressImages) {
    return executeCompressPackWorkflow(ctx, dirPath, outputDir);
  }
  return executeDirectPackWorkflow(ctx, dirPath, outputDir);
}
