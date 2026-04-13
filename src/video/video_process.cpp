#include "video/video_process.h"

#include "core/parallel.h"
#include "core/progress.h"
#include "encode/encode_config.h"
#include "pack/pack_service.h"
#include "pack/packer.h"
#include "utils/utils.h"
#include "video/encoding_batch_state.h"
#include "video/video_info.h"

#include <boost/lambda2.hpp>
#include <boost/parser/parser.hpp>
#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <print>
#include <ranges>
#include <thread>
#include <unordered_map>

namespace fs = std::filesystem;
using namespace boost::lambda2;
using namespace indicators;

auto getEncodingProgress(appctx::AppContext& ctx, appctx::EncodingState& state)
  -> std::optional<float>;

namespace {

constexpr auto kWebpTargetMaxSize = std::uintmax_t{20ULL * 1024ULL * 1024ULL};
constexpr auto kWebpMinQuality = 20;
constexpr auto kWebpQualityStep = 10;
constexpr auto kWebpFineQualityStep = 5;
constexpr auto kWebpSmallGapThreshold = std::uintmax_t{3ULL * 1024ULL * 1024ULL};

struct WebpEncodeContext {
  fs::path inputVidPath;
  fs::path outputFilePath;
  fs::path progressFilePath;
  std::function<void(std::string const&)> statusUpdater;
};

struct WebpEncodeStep {
  int exitCode;
  std::optional<std::uintmax_t> outputSize;
};

struct BatchContext {
  appctx::AppContext& app;
  EncodingBatchState& batch;
  std::span<fs::path const> vids;
  appctx::path_map<fs::path> const& plannedOutputFiles;

  auto& counters() { return batch.counters; }
  auto const& counters() const { return batch.counters; }
  auto& slots() { return batch.slots; }
  auto const& slots() const { return batch.slots; }
  auto& progress() { return batch.progressCtx; }
  auto const& progress() const { return batch.progressCtx; }

  auto nextTaskIndex() {
    return counters().nextTask.fetch_add(1, std::memory_order_acq_rel);
  }

  auto total() const { return counters().total; }

  auto finished() const {
    return counters().finished.load(std::memory_order_acquire);
  }

  void markFinished() {
    counters().finished.fetch_add(1, std::memory_order_release);
  }

  auto barIndex(std::size_t slot) const { return slots().barIndexes[slot]; }

  void setActive(std::size_t slot, appctx::EncodingStatePtr const& vidState) {
    auto lock = std::scoped_lock{slots().activeMtx};
    slots().active[slot] = vidState;
  }

  void clearActive(std::size_t slot) {
    auto lock = std::scoped_lock{slots().activeMtx};
    slots().active[slot].reset();
  }

  auto activeStates() -> appctx::EncodingStateList {
    using namespace std::views;
    auto activeStates = appctx::EncodingStateList{};
    auto lock = std::scoped_lock{slots().activeMtx};
    activeStates.reserve(slots().active.size());
    for (auto const& [_, activeState]: enumerate(slots().active)) {
      if (activeState) { activeStates.push_back(activeState); }
    }
    return activeStates;
  }

  void barEncodingStart(
    appctx::EncodingState& vidState,
    std::string_view fileLabel
  ) {
    if (!vidState.barIndex.has_value()) { return; }
    auto const index = vidState.barIndex.value();
    progress().setPostfixText(index, std::format("Encoding: {}", fileLabel));
    progress().setProgress(index, 0.0f);
  }

  void barEncodingStatus(
    appctx::EncodingState& vidState,
    std::string_view fileLabel,
    std::string_view status
  ) {
    if (!vidState.barIndex.has_value()) { return; }
    auto const index = vidState.barIndex.value();
    progress().setPostfixText(
      index,
      std::format("Encoding: {} | {}", fileLabel, status)
    );
  }

  void barIdle(std::size_t barIndex, std::size_t slot) {
    progress().setProgress(barIndex, 100.0f);
    progress().setPostfixText(
      barIndex,
      std::format("Encoding: [idle-{}]", slot + 1)
    );
  }

  void updateOverall() {
    if (!counters().overallBarIndex.has_value()) { return; }

    auto activeProgress = 0.0f;
    {
      auto const activeList = activeStates();
      for (auto const& activeState: activeList) {
        if (!activeState) { continue; }
        auto stateLock = std::scoped_lock{activeState->mtx};
        if (activeState->lastProgress.has_value()) {
          activeProgress += activeState->lastProgress.value() / 100.0f;
        }
      }
    }

    auto const completed = finished();
    auto const totalCount = static_cast<float>(total());
    auto overallPercent = 0.0f;
    if (totalCount > 0.0f) {
      overallPercent =
        std::min(100.0f, (completed + activeProgress) / totalCount * 100.0f);
    }

    progress().setProgress(counters().overallBarIndex.value(), overallPercent);
    progress().setPostfixText(
      counters().overallBarIndex.value(),
      std::format("Overall: {}/{}", completed, total())
    );
  }

  void recordResult(appctx::EncodingState const& vidState, bool result) {
    auto lock = std::scoped_lock{batch.results.mtx};
    batch.results.map[vidState.inputPath] = result;
  }

  void finalizeState(appctx::EncodingStatePtr const& vidState, bool result) {
    if (result) {
      auto lock = std::scoped_lock{vidState->mtx};
      if (
        vidState->plannedOutputFile.has_value()
        && fs::exists(vidState->plannedOutputFile.value())
      ) {
        vidState->outputFile = vidState->plannedOutputFile;
      }
    }

    {
      auto lock = std::scoped_lock{vidState->mtx};
      vidState->finished = true;
      vidState->success = result;
      vidState->endTime = std::chrono::steady_clock::now();
      vidState->lastProgress = 100.0f;
    }

    {
      auto lock = std::scoped_lock{vidState->mtx};
      if (vidState->progressFilePath.has_value()) {
        auto ec = std::error_code{};
        fs::remove(vidState->progressFilePath.value(), ec);
      }
    }
  }
};

auto packEncodedVideos(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  std::unordered_map<fs::path, bool> const& vidsRunRes
) -> int;

auto truncateForProgressLabel(std::string const& text, std::size_t maxLen = 48)
  -> std::string {
  if (text.size() <= maxLen) { return text; }
  if (maxLen <= 3) { return text.substr(0, maxLen); }
  return std::format("{}...", text.substr(0, maxLen - 3));
}

auto containsCaseInsensitive(std::string_view text, std::string_view needle)
  -> bool {
  if (needle.empty()) { return true; }
  if (text.size() < needle.size()) { return false; }

  for (std::size_t i = 0; i + needle.size() <= text.size(); ++i) {
    auto match = true;
    for (std::size_t j = 0; j < needle.size(); ++j) {
      auto const tc = static_cast<unsigned char>(text[i + j]);
      auto const nc = static_cast<unsigned char>(needle[j]);
      if (std::tolower(tc) != std::tolower(nc)) {
        match = false;
        break;
      }
    }
    if (match) { return true; }
  }

  return false;
}

auto isLikelyFfmpegErrorLine(std::string_view line) -> bool {
  return containsCaseInsensitive(line, "error")
      || containsCaseInsensitive(line, "failed")
      || containsCaseInsensitive(line, "invalid")
      || containsCaseInsensitive(line, "not found");
}

auto makeSlotLabel(fs::path const& vidPath) -> std::string {
  return truncateForProgressLabel(vidPath.filename().string());
}

auto getStateLabel(appctx::EncodingState const& state) -> std::string {
  return truncateForProgressLabel(state.inputPath.filename().string());
}

auto normalizeSourceRootDir(fs::path const& inputPath) -> fs::path {
  return fs::is_directory(inputPath) ? inputPath : inputPath.parent_path();
}

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

auto relativeParentPath(
  std::optional<fs::path> const& sourceRootDir,
  fs::path const& inputPath
) -> std::optional<fs::path> {
  if (!sourceRootDir.has_value()) { return std::nullopt; }

  auto const relativePath =
    inputPath.parent_path().lexically_relative(sourceRootDir.value());
  if (relativePath.empty() || relativePath == fs::path{"."}) { return std::nullopt; }

  return relativePath;
}

auto buildCollisionGroupLabel(
  std::optional<fs::path> const& sourceRootDir,
  fs::path const& inputPath
) -> std::string {
  auto label = std::string{};
  if (
    auto const relativePath = relativeParentPath(sourceRootDir, inputPath);
    relativePath.has_value()
  ) {
    label = sanitizeLabel(relativePath->generic_string());
  }

  if (label.empty() && inputPath.has_extension()) {
    auto const extension = inputPath.extension().string();
    auto const extensionView =
      std::string_view{extension}.substr(extension.starts_with('.') ? 1 : 0);
    label = sanitizeLabel(extensionView);
  }

  if (label.empty()) { label = "src"; }

  return label;
}

auto shouldForceConflictNaming(appctx::AppConfig const& config) -> bool {
  return config.forceNameConflictHandling
      && config.outputLayout == appctx::OutputLayout::Flat;
}

auto buildConflictHandledOutputPath(
  std::optional<fs::path> const& sourceRootDir,
  fs::path const& inputPath,
  fs::path const& candidatePath
) -> fs::path {
  auto const stem = candidatePath.stem().string();
  auto const extension = candidatePath.extension().string();
  return candidatePath.parent_path()
       / std::format(
           "{}__{}__{}{}",
           buildCollisionGroupLabel(sourceRootDir, inputPath),
           stem,
           shortPathHash(inputPath),
           extension
         );
}

auto resolveOutputRootDir(
  appctx::AppConfig const& config,
  std::optional<fs::path> const& sourceRootDir
) -> std::optional<fs::path> {
  if (config.outputPath.has_value()) { return config.outputPath.value(); }
  if (config.outputFormat != "webp" || !sourceRootDir.has_value()) {
    return std::nullopt;
  }

  return sourceRootDir.value() / "encoded_webp";
}

auto resolvePlannedOutputDir(
  appctx::AppConfig const& config,
  fs::path const& inputPath,
  std::optional<fs::path> const& sourceRootDir,
  std::optional<fs::path> const& outputRootDir
) -> fs::path {
  if (!outputRootDir.has_value()) { return inputPath.parent_path(); }

  auto outputDir = outputRootDir.value();
  if (config.outputLayout != appctx::OutputLayout::Keep) { return outputDir; }

  if (
    auto const relativePath = relativeParentPath(sourceRootDir, inputPath);
    relativePath.has_value()
  ) {
    outputDir /= relativePath.value();
  }

  return outputDir;
}

auto ensureUniqueOutputPaths(appctx::path_map<fs::path>& plannedOutputFiles)
  -> void {
  while (true) {
    auto duplicateGroups = appctx::path_map<std::vector<fs::path>>{};
    duplicateGroups.reserve(plannedOutputFiles.size());

    for (auto const& [inputPath, outputPath]: plannedOutputFiles) {
      duplicateGroups[outputPath].push_back(inputPath);
    }

    auto hadDuplicates = false;
    for (auto const& [outputPath, inputPaths]: duplicateGroups) {
      if (inputPaths.size() < 2) { continue; }

      hadDuplicates = true;
      auto const stem = outputPath.stem().string();
      auto const extension = outputPath.extension().string();
      for (auto const& inputPath: inputPaths) {
        plannedOutputFiles[inputPath] = outputPath.parent_path()
          / std::format("{}__{}{}", stem, shortPathHash(inputPath), extension);
      }
    }

    if (!hadDuplicates) { return; }
  }
}

auto lookupPlannedOutputFile(
  appctx::path_map<fs::path> const& plannedOutputFiles,
  fs::path const& inputPath
) -> std::optional<fs::path> {
  if (
    auto const it = plannedOutputFiles.find(inputPath);
    it != plannedOutputFiles.end()
  ) {
    return it->second;
  }

  return std::nullopt;
}

void registerEncodingState(
  appctx::RuntimeContext& runtime,
  appctx::EncodingStatePtr const& state
) {
  auto lock = std::scoped_lock{runtime.encodingStates.mtx};
  runtime.encodingStates.map[state->inputPath] = state;
}

auto createEncodingState(
  BatchContext& batchCtx,
  fs::path const& vidPath,
  std::size_t barIndex
) -> appctx::EncodingStatePtr {
  auto vidState = std::make_shared<appctx::EncodingState>();
  vidState->inputPath = vidPath;
  vidState->barIndex = barIndex;
  vidState->startTime = std::chrono::steady_clock::now();
  vidState->progressFilePath =
    fs::temp_directory_path() / std::format("progress_{}.txt", getUUID());
  vidState->plannedOutputFile =
    lookupPlannedOutputFile(batchCtx.plannedOutputFiles, vidPath);
  if (vidState->plannedOutputFile.has_value()) {
    vidState->outputPath = vidState->plannedOutputFile->parent_path();
  }
  registerEncodingState(batchCtx.app.runtime, vidState);
  return vidState;
}

auto startEncodingMonitor(BatchContext& batchCtx) -> std::jthread {
  return std::jthread([&] {
    using namespace std::chrono_literals;

    while (batchCtx.finished() < batchCtx.total()) {
      auto const activeStates = batchCtx.activeStates();

      for (auto const& activeState: activeStates) {
        if (!activeState) { continue; }

        auto const progress = getEncodingProgress(batchCtx.app, *activeState);
        if (!progress.has_value()) {
          auto barIndex = std::optional<std::size_t>{};
          auto lastError = std::optional<std::string>{};
          auto lastStatus = std::optional<std::string>{};
          {
            auto lock = std::scoped_lock{activeState->mtx};
            barIndex = activeState->barIndex;
            lastError = activeState->lastError;
            lastStatus = activeState->lastStatus;
          }

          if (barIndex.has_value()) {
            auto const fileLabel = getStateLabel(*activeState);
            if (lastError.has_value()) {
              batchCtx.progress().setPostfixText(
                barIndex.value(),
                std::format("Encoding: {} | {}", fileLabel, lastError.value())
              );
            } else if (lastStatus.has_value()) {
              batchCtx.progress().setPostfixText(
                barIndex.value(),
                std::format("Encoding: {} | {}", fileLabel, lastStatus.value())
              );
            }
          }

          continue;
        }

        auto barIndex = std::optional<std::size_t>{};
        {
          auto lock = std::scoped_lock{activeState->mtx};
          activeState->lastProgress = progress.value();
          barIndex = activeState->barIndex;
        }

        if (barIndex.has_value()) {
          batchCtx.progress().setProgress(barIndex.value(), progress.value());
        }
      }

      batchCtx.updateOverall();

      std::this_thread::sleep_for(20ms);
    }

    batchCtx.updateOverall();
  });
}

void runEncodingSlot(BatchContext& batchCtx, std::size_t slot) {
  while (true) {
    auto const taskIndex = batchCtx.nextTaskIndex();
    if (taskIndex >= batchCtx.total()) { break; }

    auto const& vidPath = batchCtx.vids[taskIndex];
    spdlog::debug(
      "[slot:{} task:{}/{}] start encoding: {}",
      slot + 1,
      taskIndex + 1,
      batchCtx.total(),
      vidPath.string()
    );
    auto const barIndex = batchCtx.barIndex(slot);
    auto vidState = createEncodingState(batchCtx, vidPath, barIndex);
    batchCtx.setActive(slot, vidState);

    auto const fileLabel = makeSlotLabel(vidPath);
    batchCtx.barEncodingStart(*vidState, fileLabel);
    auto const result =
      encodeToHevc(batchCtx.app, *vidState, [&](std::string const& status) {
        batchCtx.barEncodingStatus(*vidState, fileLabel, status);
        auto lock = std::scoped_lock{vidState->mtx};
        vidState->lastStatus = status;
      });

    batchCtx.recordResult(*vidState, result);
    batchCtx.finalizeState(vidState, result);

    auto outputFile = std::optional<fs::path>{};
    auto elapsedMs = int64_t{0};
    {
      auto lock = std::scoped_lock{vidState->mtx};
      outputFile = vidState->outputFile;
      if (vidState->startTime.has_value() && vidState->endTime.has_value()) {
        using namespace std::chrono;
        auto const elapsed = vidState->endTime.value() - vidState->startTime.value();
        elapsedMs = duration_cast<milliseconds>(elapsed).count();
      }
    }

    if (result) {
      spdlog::info(
        "[slot:{} task:{}/{}] encoded success: {} -> {} ({} ms)",
        slot + 1,
        taskIndex + 1,
        batchCtx.total(),
        vidPath.string(),
        outputFile.has_value() ? outputFile->string() : "<unknown>",
        elapsedMs
      );
    } else {
      spdlog::warn(
        "[slot:{} task:{}/{}] encoded failed: {} ({} ms)",
        slot + 1,
        taskIndex + 1,
        batchCtx.total(),
        vidPath.string(),
        elapsedMs
      );
    }

    batchCtx.barIdle(barIndex, slot);
    batchCtx.clearActive(slot);

    batchCtx.markFinished();
    batchCtx.updateOverall();
  }
}

void printNoEncodableVideosMessage(
  appctx::AppConfig const& config,
  appctx::ToolchainPaths const& toolchain,
  fs::path const& inputPath
) {
  if (fs::is_regular_file(inputPath)) {
    if (config.outputFormat == "mp4" && isHevcEncoded(toolchain, inputPath)) {
      std::println("Video is already HEVC encoded: {}", inputPath.string());
    } else {
      std::println("No encodable videos found for file: {}", inputPath.string());
    }
  } else {
    std::println("No encodable videos found in path: {}", inputPath.string());
  }
}

void clearWebpStaleFiles(
  fs::path const& progressFilePath,
  fs::path const& outputFile
) {
  auto ec = std::error_code{};
  if (fs::exists(progressFilePath, ec)) { fs::remove(progressFilePath, ec); }
  if (fs::exists(outputFile, ec)) { fs::remove(outputFile, ec); }
}

auto runWebpEncodingStep(
  appctx::AppContext const& appCtx,
  WebpEncodeContext const& encodeCtx,
  uint8_t quality,
  fs::path const& outputFile
) -> WebpEncodeStep {
  clearWebpStaleFiles(encodeCtx.progressFilePath, outputFile);

  spdlog::debug(
    "WebP encoding step: input={} quality={} output={}",
    encodeCtx.inputVidPath.string(),
    quality,
    outputFile.string()
  );

  auto const cfg = EncodeConfig{
    .ffmpegPath = appCtx.toolchain.ffmpegPath,
    .inputPath = encodeCtx.inputVidPath,
    .outputFilePath = outputFile,
    .outputFormat = appCtx.config.outputFormat,
    .webpQuality = quality,
    .progressFilePath = encodeCtx.progressFilePath
  };

  if (auto const res = cfg.validate(); !res) {
    spdlog::error(res.error());
    return {-1, std::nullopt};
  }

  auto const [exitCode, _] = exec2(cfg.buildCMD(), [&](std::string_view line) {
    if (!encodeCtx.statusUpdater || !isLikelyFfmpegErrorLine(line)) { return; }
    encodeCtx.statusUpdater(truncateForProgressLabel(std::string{line}, 72));
  });
  if (exitCode != 0) {
    spdlog::warn(
      "WebP encoding step failed: input={} quality={} exitCode={}",
      encodeCtx.inputVidPath.string(),
      quality,
      exitCode
    );
    return {exitCode, std::nullopt};
  }
  if (!fs::exists(outputFile)) { return {exitCode, std::nullopt}; }

  spdlog::debug(
    "WebP encoding step output size: input={} quality={} bytes={}",
    encodeCtx.inputVidPath.string(),
    quality,
    fs::file_size(outputFile)
  );

  return {exitCode, fs::file_size(outputFile)};
}

auto encodeWebpWithTargetSize(
  appctx::AppContext const& appCtx,
  WebpEncodeContext const& encodeCtx
) -> bool {
  auto const outputFile = encodeCtx.outputFilePath;

  spdlog::debug(
    "WebP adaptive encoding start: input={} output={} target={} bytes",
    encodeCtx.inputVidPath.string(),
    outputFile.string(),
    kWebpTargetMaxSize
  );

  auto const qualityStepForSize = [](std::uintmax_t outputSize) {
    auto const sizeGap = outputSize - kWebpTargetMaxSize;
    return sizeGap <= kWebpSmallGapThreshold ? kWebpFineQualityStep
                                             : kWebpQualityStep;
  };

  auto lastExitCode = -1;
  auto quality = 80u;
  while (quality >= kWebpMinQuality) {
    if (encodeCtx.statusUpdater) {
      encodeCtx.statusUpdater(std::format("q={}", quality));
    }
    auto const stepRes = runWebpEncodingStep(appCtx, encodeCtx, quality, outputFile);
    lastExitCode = stepRes.exitCode;
    if (!stepRes.outputSize.has_value()) { continue; }

    auto const outputSize = stepRes.outputSize.value();
    if (outputSize < kWebpTargetMaxSize) {
      spdlog::debug(
        "WebP encoded under target size: {} ({} bytes, q={})",
        outputFile.string(),
        outputSize,
        quality
      );
      return true;
    }

    auto const step = qualityStepForSize(outputSize);
    auto const nextQuality = quality - step;
    if (encodeCtx.statusUpdater && nextQuality >= kWebpMinQuality) {
      auto const outputSizeMB = static_cast<double>(outputSize) / 1024.0 / 1024.0;
      encodeCtx.statusUpdater(
        std::format("retry q={} ({:.1f}MB)", nextQuality, outputSizeMB)
      );
    }

    quality -= step;
  }

  if (lastExitCode == 0 && fs::exists(outputFile)) {
    spdlog::warn(
      "WebP encoding reached minimum quality but still over target: input={} "
      "output={} bytes={}",
      encodeCtx.inputVidPath.string(),
      outputFile.string(),
      fs::file_size(outputFile)
    );
    if (encodeCtx.statusUpdater) {
      auto const outputSizeMB =
        static_cast<double>(fs::file_size(outputFile)) / 1024.0 / 1024.0;
      encodeCtx.statusUpdater(std::format("min-q reached ({:.1f}MB)", outputSizeMB));
    }
    return true;
  }

  spdlog::error(
    "WebP adaptive encoding failed: input={} output={}",
    encodeCtx.inputVidPath.string(),
    outputFile.string()
  );

  return false;
}

auto tryReadProgressData(fs::path const& progressFilePath)
  -> std::optional<ProgressData> {
  if (!fs::exists(progressFilePath)) { return std::nullopt; }
  return parseProgressFile(progressFilePath);
}

auto scanInputVideos(appctx::AppContext& ctx, fs::path const& inputPath)
  -> std::vector<fs::path> {
  std::println("Scanning input path for videos: {} ...", inputPath.string());
  spdlog::info("Scanning input path: {}", inputPath.string());
  auto vids = readAllVids(ctx.config, ctx.toolchain, ctx.runtime, inputPath);
  std::println("Video scan completed, found {} candidate file(s).", vids.size());
  spdlog::info("Scan completed: {} candidate video(s)", vids.size());
  return vids;
}

auto scanInputVideosFromFiles(
  appctx::AppContext& ctx,
  std::span<fs::path const> inputPaths
) -> std::vector<fs::path> {
  std::println("Scanning input files for videos: {} file(s) ...", inputPaths.size());
  spdlog::info("Scanning {} provided input file(s)", inputPaths.size());
  auto vids =
    readAllVidsFromFiles(ctx.config, ctx.toolchain, ctx.runtime, inputPaths);
  std::println("Video scan completed, found {} candidate file(s).", vids.size());
  spdlog::info("Scan completed from files: {} candidate video(s)", vids.size());
  return vids;
}

auto resolveMultiInputBasePath(
  appctx::AppConfig const& config,
  std::span<fs::path const> inputPaths
) -> std::optional<fs::path> {
  if (inputPaths.empty()) { return std::nullopt; }

  if (config.outputPath.has_value()) { return config.outputPath.value(); }

  auto const basePath = inputPaths.front().parent_path();
  for (auto const& inputPath: inputPaths) {
    if (inputPath.parent_path() != basePath) { return std::nullopt; }
  }

  return basePath;
}

auto maybePackOutputs(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  std::unordered_map<fs::path, bool> const& vidsRunRes
) -> int {
  if (!ctx.config.packOutput) { return 0; }
  return packEncodedVideos(ctx, inputPath, plannedOutputFiles, vidsRunRes);
}

auto runEncodingWithoutProgress(
  appctx::AppContext& ctx,
  std::vector<fs::path> const& vids,
  appctx::path_map<fs::path> const& plannedOutputFiles
) -> std::unordered_map<fs::path, bool> {
  auto vidsRunRes = std::unordered_map<fs::path, bool>{};
  vidsRunRes.reserve(vids.size());

  spdlog::info(
    "Running encoding without progress bars (verbose echo mode), total={}.",
    vids.size()
  );

  for (auto const& vidPath: vids) {
    auto state = appctx::EncodingState{};
    state.inputPath = vidPath;
    state.plannedOutputFile = lookupPlannedOutputFile(plannedOutputFiles, vidPath);
    if (state.plannedOutputFile.has_value()) {
      state.outputPath = state.plannedOutputFile->parent_path();
    }

    spdlog::debug("Start encoding (no-progress): {}", vidPath.string());

    auto const success = encodeToHevc(ctx, state, {});
    vidsRunRes[vidPath] = success;
    if (success) {
      spdlog::info("Encoded success (no-progress): {}", vidPath.string());
    } else {
      spdlog::warn("Encoded failed (no-progress): {}", vidPath.string());
    }
  }

  return vidsRunRes;
}

auto runEncodingBatches(
  appctx::AppContext& ctx,
  std::vector<fs::path> const& vids,
  appctx::path_map<fs::path> const& plannedOutputFiles
) -> std::optional<std::unordered_map<fs::path, bool>> {
  constexpr auto kMaxConcurrentJobs = std::size_t{10};

  spdlog::info(
    "Preparing encoding batch: total={} output-format={} pack-output={}",
    vids.size(),
    ctx.config.outputFormat,
    ctx.config.packOutput
  );

  auto const proceed = readUserIpt(
    ctx.config.yesToAll,
    std::format(
      "do you want to encode the video to {} format? (y/N): ",
      ctx.config.outputFormat
    )
  );
  if (!proceed) {
    std::println("Encoding tasks canceled by user.");
    spdlog::info("Encoding canceled by user.");
    return std::nullopt;
  }

  if (ctx.config.verbose && ctx.config.verboseEcho) {
    std::println("Verbose echo enabled: progress bars are disabled.");
    spdlog::debug("Progress bars disabled due to verbose echo mode.");
    return runEncodingWithoutProgress(ctx, vids, plannedOutputFiles);
  }

  auto _ = progress::CursorGuard{};

  auto const maxConcurrentJobs = std::max<std::size_t>(
    1,
    ctx.config.maxParallelJobs.value_or(kMaxConcurrentJobs)
  );
  auto const workerCount = std::min(vids.size(), maxConcurrentJobs);
  auto state = EncodingBatchState{vids.size(), workerCount};

  std::println(
    "Scheduling {} video(s) with max {} concurrent encode job(s)...",
    vids.size(),
    workerCount
  );
  spdlog::info(
    "Scheduling encoding workers: workers={} tasks={}",
    workerCount,
    vids.size()
  );

  auto batchCtx = BatchContext{ctx, state, vids, plannedOutputFiles};
  batchCtx.updateOverall();
  auto monitorThread = startEncodingMonitor(batchCtx);

  parallel::runIndexedTasks(workerCount, workerCount, [&](std::size_t slot) {
    runEncodingSlot(batchCtx, slot);
  });

  monitorThread.join();

  spdlog::info("Encoding batch completed: processed={}.", state.results.map.size());

  return std::move(state.results.map);
}

auto collectEncodedOutputFiles(
  appctx::AppContext& ctx,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  std::unordered_map<fs::path, bool> const& vidsRunRes
) -> std::vector<EncodedVideoPackFile> {
  constexpr auto kWebpPackMaxSize = std::uintmax_t{20ULL * 1024ULL * 1024ULL};

  auto encodedOutputFiles = std::vector<EncodedVideoPackFile>{};
  encodedOutputFiles.reserve(vidsRunRes.size());
  spdlog::debug(
    "Collecting encoded outputs for packing: success-map-size={}",
    vidsRunRes.size()
  );
  for (auto const& [vidPath, success]: vidsRunRes) {
    if (!success) { continue; }

    auto const outFile = lookupPlannedOutputFile(plannedOutputFiles, vidPath);
    if (!outFile.has_value() || !fs::exists(outFile.value())) { continue; }

    if (
      ctx.config.outputFormat == "webp"
      && fs::file_size(outFile.value()) >= kWebpPackMaxSize
    ) {
      std::println(
        "Skipping oversized webp for packing: {} ({} bytes)",
        outFile.value().string(),
        fs::file_size(outFile.value())
      );
      continue;
    }

    encodedOutputFiles.emplace_back(EncodedVideoPackFile{
      .sourcePath = vidPath,
      .outputPath = outFile.value(),
    });
  }

  return encodedOutputFiles;
}

auto packEncodedVideos(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  std::unordered_map<fs::path, bool> const& vidsRunRes
) -> int {
  spdlog::info("Packing encoded outputs for input: {}", inputPath.string());
  auto const encodedOutputFiles =
    collectEncodedOutputFiles(ctx, plannedOutputFiles, vidsRunRes);
  if (encodedOutputFiles.empty()) {
    std::println("No encoded output files found to pack.");
    return 0;
  }

  auto const groupedFiles = groupEncodedVideosForPack(encodedOutputFiles);
  auto const zipOutputDir = resolveVideoPackOutputPath(ctx.config, inputPath);

  fs::create_directories(zipOutputDir);

  std::println(
    "Packing {} encoded video(s) into {} archive(s)...",
    encodedOutputFiles.size(),
    groupedFiles.size()
  );
  spdlog::info(
    "Packing plan: files={} archives={} output-dir={}",
    encodedOutputFiles.size(),
    groupedFiles.size(),
    zipOutputDir.string()
  );

  auto const ordinalRanges = pack::buildGroupOrdinalRanges(groupedFiles);

  auto const plan = pack::PackPlan{
    .groups = groupedFiles,
    .outputDir = zipOutputDir,
    .zipNameForIndex =
      [ordinalRanges](std::size_t index) {
        return pack::appendOrdinalRangeSuffix(
          std::format("encoded_videos_part{}.zip", index + 1),
          ordinalRanges.at(index)
        );
      },
    .progressLabelForIndex =
      [ordinalRanges](std::size_t index) {
        return std::format(
          "Packing: {}",
          pack::appendOrdinalRangeSuffix(
            std::format("encoded_videos_part{}.zip", index + 1),
            ordinalRanges.at(index)
          )
        );
      },
    .maxParallelJobs = ctx.config.maxParallelJobs
  };

  auto const packRes = pack::packGroupsParallel(plan);
  if (!packRes) {
    spdlog::error("Failed to pack encoded videos: {}", packRes.error());
    return 1;
  }

  for (auto const& zipPath: packRes.value()) {
    if (!zipPath.empty()) { std::println("Packed archive: {}", zipPath.string()); }
  }

  spdlog::info("Packing completed: archive-count={}", packRes.value().size());

  return 0;
}

void printEncodingSummary(
  std::span<fs::path const> vids,
  std::unordered_map<fs::path, bool> const& vidsRunRes
) {
  namespace rng = std::ranges;

  std::println("All encoding tasks completed.");
  std::println("Summary:");
  std::println("\tTotal videos found: {}", vids.size());

  auto const successCount = rng::count_if(vidsRunRes, _1->*second);
  auto const failureCount = vidsRunRes.size() - successCount;

  spdlog::info(
    "Encoding summary: total={} success={} failed={}",
    vids.size(),
    successCount,
    failureCount
  );

  std::println("\tSuccessfully encoded: {}", successCount);
  std::println("\tFailed to encode: {}", failureCount);

  if (failureCount > 0) {
    std::println("Videos that failed to encode:");
    for (auto const& [vidPath, success]: vidsRunRes) {
      if (!success) { std::println("\t{}", vidPath.string()); }
    }
  }
}

}  // namespace

auto planVideoOutputFiles(
  appctx::AppConfig const& config,
  std::span<fs::path const> inputPaths,
  std::optional<fs::path> sourceRootDir
) -> eh::Result<appctx::path_map<fs::path>> {
  auto plannedOutputFiles = appctx::path_map<fs::path>{};
  plannedOutputFiles.reserve(inputPaths.size());

  if (inputPaths.empty()) { return plannedOutputFiles; }

  auto const outputRootDir = resolveOutputRootDir(config, sourceRootDir);
  auto const usesSharedOutputRoot = outputRootDir.has_value();

  if (
    usesSharedOutputRoot
    && config.outputLayout == appctx::OutputLayout::Keep
    && !sourceRootDir.has_value()
  ) {
    return eh::makeError(
      "--keep requires input files to share a common parent directory."
    );
  }

  auto groupedCandidates = appctx::path_map<std::vector<fs::path>>{};
  groupedCandidates.reserve(inputPaths.size());
  auto const forceConflictNaming = shouldForceConflictNaming(config);

  for (auto const& inputPath: inputPaths) {
    auto const outputDir =
      resolvePlannedOutputDir(config, inputPath, sourceRootDir, outputRootDir);
    auto const fileName =
      EncodeConfig{.inputPath = inputPath, .outputFormat = config.outputFormat}
        .buildOutputFileName();
    groupedCandidates[outputDir / fileName].push_back(inputPath);
  }

  for (auto const& [candidatePath, groupedInputs]: groupedCandidates) {
    if (groupedInputs.size() == 1 && !forceConflictNaming) {
      plannedOutputFiles[groupedInputs.front()] = candidatePath;
      continue;
    }

    auto sortedInputs = groupedInputs;
    std::ranges::sort(sortedInputs, [](fs::path const& lhs, fs::path const& rhs) {
      return stablePathString(lhs) < stablePathString(rhs);
    });

    auto const stem = candidatePath.stem().string();
    auto const extension = candidatePath.extension().string();
    for (auto const& inputPath: sortedInputs) {
      plannedOutputFiles[inputPath] = buildConflictHandledOutputPath(
        sourceRootDir,
        inputPath,
        candidatePath
      );
    }
  }

  ensureUniqueOutputPaths(plannedOutputFiles);

  return plannedOutputFiles;
}

auto resolveVideoOutputPath(
  appctx::AppConfig const& config,
  fs::path const& inputPath
) -> std::optional<fs::path> {
  if (config.outputPath.has_value()) { return config.outputPath; }

  if (config.outputFormat != "webp") { return std::nullopt; }

  auto const basePath =
    fs::is_directory(inputPath) ? inputPath : inputPath.parent_path();
  return basePath / "encoded_webp";
}

auto resolveVideoPackOutputPath(
  appctx::AppConfig const& config,
  fs::path const& inputPath
) -> fs::path {
  if (config.outputPath.has_value()) { return config.outputPath.value() / "packed"; }

  auto const basePath =
    fs::is_directory(inputPath) ? inputPath : inputPath.parent_path();
  return basePath / "packed";
}

auto groupEncodedVideosForPack(std::vector<fs::path> const& filePaths)
  -> std::vector<std::vector<fs::path>> {
  constexpr auto kMaxZipSize = std::uintmax_t{500 * 1024 * 1024};
  auto packInputs = std::vector<PackGroupInput>{};
  packInputs.reserve(filePaths.size());
  for (auto const& filePath: filePaths) {
    packInputs.emplace_back(PackGroupInput{filePath, filePath.parent_path()});
  }

  return groupPackFiles(packInputs, kMaxZipSize);
}

auto groupEncodedVideosForPack(std::vector<EncodedVideoPackFile> const& filePaths)
  -> std::vector<std::vector<fs::path>> {
  constexpr auto kMaxZipSize = std::uintmax_t{500 * 1024 * 1024};
  auto packInputs = std::vector<PackGroupInput>{};
  packInputs.reserve(filePaths.size());
  for (auto const& file: filePaths) {
    packInputs.emplace_back(
      PackGroupInput{file.outputPath, file.sourcePath.parent_path()}
    );
  }

  return groupPackFiles(packInputs, kMaxZipSize);
}

bool encodeToHevc(
  appctx::AppContext& ctx,
  appctx::EncodingState& state,
  function_ref statusUpdater
) {
  auto progressFilePath = fs::path{};
  auto outputPath = std::optional<fs::path>{};
  auto plannedOutputFile = std::optional<fs::path>{};
  {
    auto lock = std::scoped_lock{state.mtx};
    if (!state.progressFilePath.has_value()) {
      state.progressFilePath =
        fs::temp_directory_path() / std::format("progress_{}.txt", getUUID());
    }
    progressFilePath = state.progressFilePath.value();
    outputPath = state.outputPath;
    plannedOutputFile = state.plannedOutputFile;
  }

  if (!plannedOutputFile.has_value()) {
    auto const error = std::format(
      "Failed to resolve output file for input: {}",
      state.inputPath.string()
    );
    {
      auto lock = std::scoped_lock{state.mtx};
      state.lastError = error;
    }
    spdlog::error(error);
    return false;
  }

  {
    auto ec = std::error_code{};
    fs::remove(progressFilePath, ec);
  }

  fs::create_directories(plannedOutputFile->parent_path());

  auto const cfg = EncodeConfig{
    .ffmpegPath = ctx.toolchain.ffmpegPath,
    .inputPath = state.inputPath,
    .outputPath = outputPath,
    .outputFilePath = plannedOutputFile,
    .outputFormat = ctx.config.outputFormat,
    .progressFilePath = progressFilePath
  };

  spdlog::debug(
    "Encode config: input={} output-format={} output-file={} progress-file={}",
    state.inputPath.string(),
    ctx.config.outputFormat,
    plannedOutputFile->string(),
    progressFilePath.string()
  );

  auto const validationResult = cfg.validate();
  if (!validationResult) {
    {
      auto lock = std::scoped_lock{state.mtx};
      state.lastError = validationResult.error();
    }
    spdlog::error(validationResult.error());
    return false;
  }

  spdlog::debug("Encoding video: {}", state.inputPath.string());

  if (ctx.config.outputFormat == "webp") {
    return encodeWebpWithTargetSize(
      ctx,
      WebpEncodeContext{
        .inputVidPath = state.inputPath,
        .outputFilePath = plannedOutputFile.value(),
        .progressFilePath = progressFilePath,
        .statusUpdater = statusUpdater
      }
    );
  }

  auto const [exitCode, _] = exec2(cfg.buildCMD(), [&](std::string_view line) {
    if (!statusUpdater || !isLikelyFfmpegErrorLine(line)) { return; }
    statusUpdater(truncateForProgressLabel(std::string{line}, 72));
  });
  if (exitCode != 0) {
    spdlog::warn(
      "ffmpeg exited with non-zero code: input={} exitCode={}",
      state.inputPath.string(),
      exitCode
    );
  }
  return exitCode == 0;
}

int handleSingleFileEncoding(appctx::AppContext& ctx, fs::path const& videoPath) {
  return handlePathEncoding(ctx, videoPath);
}

auto readLastNLines(fs::path const& filePath, std::size_t n)
  -> std::vector<std::string> {
  auto file = std::ifstream{filePath};
  if (!file.is_open() || n == 0) { return {}; }

  auto tail = std::deque<std::string>{};
  auto line = std::string{};

  while (std::getline(file, line)) {
    if (tail.size() == n) { tail.pop_front(); }
    tail.push_back(line);
  }

  return {tail.begin(), tail.end()};
}

auto parseProgressFile(fs::path const& progressFilePath) -> ProgressData {
  namespace bp = boost::parser;

  auto const lines = readLastNLines(progressFilePath, 12);
  auto frameCount = uint64_t{0};
  auto progressStatus = std::string{};

  auto const frameParser = bp::string("frame=") >> bp::uint_;
  auto const progressParser = bp::string("progress=") >> *bp::char_;

  for (auto const& line: lines) {
    if (auto const& res = parse(line, frameParser); res.has_value()) {
      auto [_, _frameCount] = res.value();
      frameCount = _frameCount;
    }
    if (auto const& res = parse(line, progressParser); res.has_value()) {
      auto [_, _progressStatus] = res.value();
      progressStatus = _progressStatus;
    }
  }

  return {frameCount, progressStatus};
}

auto getEncodingProgress(appctx::AppContext& ctx, appctx::EncodingState& state)
  -> std::optional<float> {
  auto const totalFrames = getVidTotalFrames(ctx.runtime, state.inputPath);
  if (!totalFrames.has_value()) {
    auto lock = std::scoped_lock{state.mtx};
    state.lastError = totalFrames.error();
    return std::nullopt;
  }

  auto progressFilePath = std::optional<fs::path>{};
  {
    auto lock = std::scoped_lock{state.mtx};
    progressFilePath = state.progressFilePath;
  }
  if (!progressFilePath.has_value()) { return std::nullopt; }

  auto const progressData = tryReadProgressData(progressFilePath.value());
  if (!progressData.has_value()) { return std::nullopt; }

  {
    auto lock = std::scoped_lock{state.mtx};
    state.lastFrameCount = progressData->frameCount;
  }

  return ((float)progressData->frameCount / totalFrames.value()) * 100.0f;
}

int handlePathEncoding(appctx::AppContext& ctx, fs::path const& inputPath) {
  spdlog::info("Handle path encoding: {}", inputPath.string());
  auto const vids = scanInputVideos(ctx, inputPath);

  if (vids.empty()) {
    printNoEncodableVideosMessage(ctx.config, ctx.toolchain, inputPath);
    return 0;
  }

  auto const sourceRootDir = normalizeSourceRootDir(inputPath);
  auto const plannedOutputFilesRes =
    planVideoOutputFiles(ctx.config, vids, sourceRootDir);
  if (!plannedOutputFilesRes) {
    spdlog::error(plannedOutputFilesRes.error());
    return 1;
  }

  auto const runRes = runEncodingBatches(ctx, vids, plannedOutputFilesRes.value());
  if (!runRes.has_value()) { return 0; }

  auto const& vidsRunRes = runRes.value();
  auto const packRes =
    maybePackOutputs(ctx, inputPath, plannedOutputFilesRes.value(), vidsRunRes);
  if (packRes != 0) { return packRes; }

  printEncodingSummary(vids, vidsRunRes);
  spdlog::info("Path encoding done: {}", inputPath.string());

  return 0;
}

int handleMultiFileEncoding(
  appctx::AppContext& ctx,
  std::span<fs::path const> inputPaths
) {
  spdlog::info("Handle multi-file encoding: input-count={}", inputPaths.size());
  auto const vids = scanInputVideosFromFiles(ctx, inputPaths);

  if (vids.empty()) {
    std::println("No encodable videos found in provided files.");
    return 0;
  }

  auto const basePath = resolveMultiInputBasePath(ctx.config, inputPaths);
  if (
    ctx.config.outputFormat == "webp" && !ctx.config.outputPath.has_value()
    && !basePath.has_value()
  ) {
    spdlog::error(
      "Multiple input files must share the same parent directory or specify "
      "--output/-o."
    );
    return 1;
  }

  if (
    ctx.config.outputLayout == appctx::OutputLayout::Keep
    && ctx.config.outputPath.has_value()
    && !basePath.has_value()
  ) {
    spdlog::error(
      "--keep requires multiple input files to share the same parent directory."
    );
    return 1;
  }

  auto const plannedOutputFilesRes =
    planVideoOutputFiles(ctx.config, vids, basePath);
  if (!plannedOutputFilesRes) {
    spdlog::error(plannedOutputFilesRes.error());
    return 1;
  }

  auto const runRes = runEncodingBatches(ctx, vids, plannedOutputFilesRes.value());
  if (!runRes.has_value()) { return 0; }

  auto const& vidsRunRes = runRes.value();
  if (ctx.config.packOutput) {
    if (!basePath.has_value()) {
      spdlog::error(
        "Multiple input files must share the same parent directory or specify "
        "--output/-o."
      );
      return 1;
    }

    auto const packRes = maybePackOutputs(
      ctx,
      basePath.value(),
      plannedOutputFilesRes.value(),
      vidsRunRes
    );
    if (packRes != 0) { return packRes; }
  }

  printEncodingSummary(vids, vidsRunRes);
  spdlog::info("Multi-file encoding done: input-count={}", inputPaths.size());

  return 0;
}
