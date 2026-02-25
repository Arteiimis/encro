#include "video_process.h"

#include "encode_config.h"
#include "encoding_batch_state.h"
#include "globals.h"
#include "packer.h"
#include "parallel.h"
#include "progress.h"
#include "utils.h"
#include "video_info.h"

#include <boost/lambda2.hpp>
#include <boost/parser/parser.hpp>
#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <print>
#include <ranges>
#include <thread>
#include <unordered_map>

namespace fs = std::filesystem;
using namespace boost::lambda2;
using namespace indicators;

auto monitorEncodingProgress(
  progress::ProgressContext& progressCtx,
  fs::path const& vidPath,
  std::size_t barIndex
) -> std::optional<float>;

namespace {

constexpr auto kWebpTargetMaxSize = std::uintmax_t{20ULL * 1024ULL * 1024ULL};
constexpr auto kWebpMinQuality = 20;
constexpr auto kWebpQualityStep = 10;
constexpr auto kWebpFineQualityStep = 5;
constexpr auto kWebpSmallGapThreshold = std::uintmax_t{3ULL * 1024ULL * 1024ULL};

struct ActiveBar {
  fs::path vidPath;
  std::size_t barIndex;
  std::size_t slot;
};

struct WebpEncodeContext {
  fs::path inputVidPath;
  std::optional<fs::path> outputPath;
  fs::path progressFilePath;
  std::function<void(std::string const&)> statusUpdater;
};

struct WebpEncodeStep {
  int exitCode;
  std::optional<std::uintmax_t> outputSize;
};

auto packEncodedVideos(std::unordered_map<fs::path, bool> const& vidsRunRes) -> int;

auto truncateForProgressLabel(
  std::string const& text,
  std::size_t maxLen = 48
) -> std::string {
  if (text.size() <= maxLen) { return text; }
  if (maxLen <= 3) { return text.substr(0, maxLen); }
  return std::format("{}...", text.substr(0, maxLen - 3));
}

auto makeSlotLabel(fs::path const& vidPath) -> std::string {
  return truncateForProgressLabel(vidPath.filename().string());
}

void setSlotBarEncodingStart(
  EncodingBatchState& state,
  std::size_t slot,
  std::string const& fileLabel
) {
  auto const barIndex = state.slots.barIndexes[slot];
  state.progressCtx.setPostfixText(barIndex, std::format("Encoding: {}", fileLabel));
  state.progressCtx.setProgress(barIndex, 0.0f);
}

void setSlotBarEncodingStatus(
  EncodingBatchState& state,
  std::size_t slot,
  std::string const& fileLabel,
  std::string const& status
) {
  auto const barIndex = state.slots.barIndexes[slot];
  state.progressCtx.setPostfixText(
    barIndex,
    std::format("Encoding: {} | {}", fileLabel, status)
  );
}

void setSlotBarIdle(EncodingBatchState& state, std::size_t slot) {
  auto const barIndex = state.slots.barIndexes[slot];
  state.progressCtx.setProgress(barIndex, 100.0f);
  state.progressCtx.setPostfixText(
    barIndex,
    std::format("Encoding: [idle-{}]", slot + 1)
  );
}

void resetSlotProgress(
  std::vector<float>& slotProgress,
  std::mutex& slotProgressMtx,
  std::size_t slot
) {
  auto lock = std::scoped_lock{slotProgressMtx};
  slotProgress[slot] = 0.0f;
}

auto buildActiveBars(
  std::vector<std::optional<fs::path>> const& slotTaskPaths,
  std::mutex& slotTaskPathsMtx,
  std::vector<std::size_t> const& slotBarIndexes
) -> std::vector<ActiveBar> {
  auto activeBars = std::vector<ActiveBar>{};
  auto lock = std::scoped_lock{slotTaskPathsMtx};
  activeBars.reserve(slotTaskPaths.size());
  for (auto slot = std::size_t{0}; slot < slotTaskPaths.size(); ++slot) {
    if (slotTaskPaths[slot].has_value()) {
      activeBars.push_back(
        ActiveBar{slotTaskPaths[slot].value(), slotBarIndexes[slot], slot}
      );
    }
  }
  return activeBars;
}

void updateOverallBar(EncodingBatchState& state) {
  if (!state.counters.overallBarIndex.has_value()) { return; }

  auto activeProgress = 0.0f;
  {
    auto lock = std::scoped_lock{state.slots.progressMtx};
    for (auto const progress: state.slots.progress) {
      activeProgress += progress / 100.0f;
    }
  }

  auto const completed = state.counters.finished.load(std::memory_order_acquire);
  auto const total = static_cast<float>(state.counters.total);
  auto overallPercent = 0.0f;
  if (total > 0.0f) {
    overallPercent = std::min(100.0f, (completed + activeProgress) / total * 100.0f);
  }

  state.progressCtx.setProgress(
    state.counters.overallBarIndex.value(),
    overallPercent
  );
  state.progressCtx.setPostfixText(
    state.counters.overallBarIndex.value(),
    std::format("Overall: {}/{}", completed, state.counters.total)
  );
}

void printNoEncodableVideosMessage(fs::path const& inputPath) {
  if (fs::is_regular_file(inputPath)) {
    if (GLBs.OUTPUT_FORMAT == "mp4" && isHevcEncoded(inputPath)) {
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
  WebpEncodeContext const& ctx,
  int quality,
  fs::path const& outputFile
) -> WebpEncodeStep {
  clearWebpStaleFiles(ctx.progressFilePath, outputFile);

  auto const cfg = EncodeConfig{
    .ffmpegPath = GLBs.FFMPEG_PATH,
    .inputPath = ctx.inputVidPath,
    .outputPath = ctx.outputPath,
    .outputFormat = GLBs.OUTPUT_FORMAT,
    .webpQuality = quality,
    .progressFilePath = ctx.progressFilePath
  };

  auto const validationResult = cfg.validate();
  if (!validationResult) {
    spdlog::error(validationResult.error());
    return {-1, std::nullopt};
  }

  auto const [exitCode, _] = exec2(cfg.buildCMD());
  if (exitCode != 0) { return {exitCode, std::nullopt}; }
  if (!fs::exists(outputFile)) { return {exitCode, std::nullopt}; }

  return {exitCode, fs::file_size(outputFile)};
}

auto encodeWebpWithTargetSize(WebpEncodeContext const& ctx) -> bool {
  auto const outputFile =
    EncodeConfig{
      .ffmpegPath = GLBs.FFMPEG_PATH,
      .inputPath = ctx.inputVidPath,
      .outputPath = ctx.outputPath,
      .outputFormat = GLBs.OUTPUT_FORMAT,
      .webpQuality = 80,
      .progressFilePath = ctx.progressFilePath
    }
      .buildOutputPath();

  auto const qualityStepForSize = [](std::uintmax_t outputSize) {
    auto const sizeGap = outputSize - kWebpTargetMaxSize;
    return sizeGap <= kWebpSmallGapThreshold ? kWebpFineQualityStep
                                             : kWebpQualityStep;
  };

  auto lastExitCode = -1;
  auto quality = 80;
  while (quality >= kWebpMinQuality) {
    if (ctx.statusUpdater) {
      ctx.statusUpdater(std::format("encoding q={}", quality));
    }
    auto const stepRes = runWebpEncodingStep(ctx, quality, outputFile);
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
    if (ctx.statusUpdater && nextQuality >= kWebpMinQuality) {
      auto const outputSizeMB = static_cast<double>(outputSize) / 1024.0 / 1024.0;
      ctx.statusUpdater(
        std::format("retry q={} ({:.1f}MB)", nextQuality, outputSizeMB)
      );
    }

    quality -= step;
  }

  if (lastExitCode == 0 && fs::exists(outputFile)) {
    if (ctx.statusUpdater) {
      auto const outputSizeMB =
        static_cast<double>(fs::file_size(outputFile)) / 1024.0 / 1024.0;
      ctx.statusUpdater(std::format("min-q reached ({:.1f}MB)", outputSizeMB));
    }
    return true;
  }

  return false;
}

auto tryReadProgressData(fs::path const& progressFilePath)
  -> std::optional<ProgressData> {
  if (!fs::exists(progressFilePath)) { return std::nullopt; }
  return parseProgressFile(progressFilePath);
}

auto tryGetEncodedOutputFile(
  fs::path const& vidPath,
  std::optional<fs::path> const& outputPath
) -> std::optional<fs::path> {
  auto const cfg = EncodeConfig{
    .ffmpegPath = GLBs.FFMPEG_PATH,
    .inputPath = vidPath,
    .outputPath = outputPath,
    .outputFormat = GLBs.OUTPUT_FORMAT
  };

  auto const outFile = cfg.buildOutputPath();
  if (!fs::exists(outFile)) { return std::nullopt; }
  return outFile;
}

auto scanInputVideos(fs::path const& inputPath) -> std::vector<fs::path> {
  std::println("Scanning input path for videos: {} ...", inputPath.string());
  auto vids = readAllVids(inputPath);
  std::println("Video scan completed, found {} candidate file(s).", vids.size());
  return vids;
}

auto maybePackOutputs(std::unordered_map<fs::path, bool> const& vidsRunRes) -> int {
  if (!GLBs.PACK_OUTPUT) { return 0; }
  return packEncodedVideos(vidsRunRes);
}

auto runEncodingBatches(std::vector<fs::path> const& vids)
  -> std::optional<std::unordered_map<fs::path, bool>> {
  constexpr auto kMaxConcurrentJobs = std::size_t{10};

  auto const proceed = readUserIpt(
    std::format(
      "do you want to encode the video to {} format? (y/N): ",
      GLBs.OUTPUT_FORMAT
    )
  );
  if (!proceed) {
    std::println("Encoding tasks canceled by user.");
    return std::nullopt;
  }

  auto _ = progress::CursorGuard{};

  auto const workerCount = std::min(vids.size(), kMaxConcurrentJobs);
  auto state = EncodingBatchState{vids.size(), workerCount};

  std::println(
    "Scheduling {} video(s) with max {} concurrent encode job(s)...",
    vids.size(),
    workerCount
  );

  updateOverallBar(state);

  auto monitorThread = std::jthread([&] {
    using namespace std::chrono_literals;

    while (state.counters.finished.load(std::memory_order_acquire)
           < state.counters.total) {
      auto const activeBars = buildActiveBars(
        state.slots.taskPaths,
        state.slots.taskPathsMtx,
        state.slots.barIndexes
      );

      for (auto const& activeBar: activeBars) {
        auto const progress = monitorEncodingProgress(
          state.progressCtx,
          activeBar.vidPath,
          activeBar.barIndex
        );

        if (progress.has_value()) {
          auto lock = std::scoped_lock{state.slots.progressMtx};
          state.slots.progress[activeBar.slot] = progress.value();
        }
      }

      updateOverallBar(state);

      std::this_thread::sleep_for(100ms);
    }

    updateOverallBar(state);
  });

  parallel::runIndexedTasks(workerCount, workerCount, [&](std::size_t slot) {
    while (true) {
      auto const taskIndex =
        state.counters.nextTask.fetch_add(1, std::memory_order_acq_rel);
      if (taskIndex >= state.counters.total) { break; }

      auto const& vidPath = vids[taskIndex];

      {
        auto lock = std::scoped_lock{state.slots.taskPathsMtx};
        state.slots.taskPaths[slot] = vidPath;
      }
      resetSlotProgress(state.slots.progress, state.slots.progressMtx, slot);

      auto const fileLabel = makeSlotLabel(vidPath);
      setSlotBarEncodingStart(state, slot, fileLabel);
      auto const result = encodeToHevc(vidPath, [&](std::string const& status) {
        setSlotBarEncodingStatus(state, slot, fileLabel, status);
      });

      {
        auto _ = std::scoped_lock{state.results.mtx};
        state.results.map[vidPath] = result;
      }

      setSlotBarIdle(state, slot);

      {
        auto lock = std::scoped_lock{state.slots.taskPathsMtx};
        state.slots.taskPaths[slot].reset();
      }
      resetSlotProgress(state.slots.progress, state.slots.progressMtx, slot);

      state.counters.finished.fetch_add(1, std::memory_order_release);
      updateOverallBar(state);
    }
  });

  monitorThread.join();

  return std::move(state.results.map);
}

auto collectEncodedOutputFiles(std::unordered_map<fs::path, bool> const& vidsRunRes)
  -> std::vector<fs::path> {
  constexpr auto kWebpPackMaxSize = std::uintmax_t{20ULL * 1024ULL * 1024ULL};

  auto encodedOutputFiles = std::vector<fs::path>{};
  encodedOutputFiles.reserve(vidsRunRes.size());

  auto const outputPath = resolveVideoOutputPath(GLBs.INPUT_PATH);
  for (auto const& [vidPath, success]: vidsRunRes) {
    if (!success) { continue; }

    auto const outFile = tryGetEncodedOutputFile(vidPath, outputPath);
    if (!outFile.has_value()) { continue; }

    if (GLBs.OUTPUT_FORMAT == "webp"
        && fs::file_size(outFile.value()) >= kWebpPackMaxSize) {
      std::println(
        "Skipping oversized webp for packing: {} ({} bytes)",
        outFile.value().string(),
        fs::file_size(outFile.value())
      );
      continue;
    }

    encodedOutputFiles.emplace_back(outFile.value());
  }

  return encodedOutputFiles;
}

auto packEncodedVideos(std::unordered_map<fs::path, bool> const& vidsRunRes) -> int {
  auto const encodedOutputFiles = collectEncodedOutputFiles(vidsRunRes);
  if (encodedOutputFiles.empty()) {
    std::println("No encoded output files found to pack.");
    return 0;
  }

  auto const groupedFiles = groupEncodedVideosForPack(encodedOutputFiles);
  auto const zipOutputDir = resolveVideoPackOutputPath(GLBs.INPUT_PATH);

  fs::create_directories(zipOutputDir);

  auto progressCtx = progress::ProgressContext{};
  auto packResults = std::vector<eh::Result<void>>(groupedFiles.size());
  auto zippedFiles = std::vector<fs::path>(groupedFiles.size());
  auto resultMtx = std::mutex{};

  std::println(
    "Packing {} encoded video(s) into {} archive(s)...",
    encodedOutputFiles.size(),
    groupedFiles.size()
  );

  auto _ = progress::CursorGuard{};
  parallel::runIndexedTasks(
    groupedFiles.size(),
    groupedFiles.size(),
    [&](std::size_t index) {
      auto const zipPath =
        zipOutputDir / std::format("encoded_videos_part{}.zip", index + 1);

      auto const packRes = packFilesToZip(
        groupedFiles[index],
        zipPath,
        progressCtx,
        std::format("Packing: {}", zipPath.filename().string())
      );

      auto lock = std::scoped_lock{resultMtx};
      if (!packRes) {
        packResults[index] = packRes;
        return;
      }

      packResults[index] = {};
      zippedFiles[index] = zipPath;
    }
  );

  for (auto const& res: packResults) {
    if (!res) {
      spdlog::error("Failed to pack encoded videos: {}", res.error());
      return 1;
    }
  }

  for (auto const& zipPath: zippedFiles) {
    if (!zipPath.empty()) { std::println("Packed archive: {}", zipPath.string()); }
  }

  return 0;
}

void printEncodingSummary(
  std::vector<fs::path> const& vids,
  std::unordered_map<fs::path, bool> const& vidsRunRes
) {
  namespace rng = std::ranges;

  std::println("All encoding tasks completed.");
  std::println("Summary:");
  std::println("\tTotal videos found: {}", rng::distance(vids));

  auto const successCount = rng::count_if(vidsRunRes, _1->*second);
  auto const failureCount = vidsRunRes.size() - successCount;

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

auto resolveVideoOutputPath(fs::path const& inputPath) -> std::optional<fs::path> {
  if (GLBs.OUTPUT_PATH.has_value()) { return GLBs.OUTPUT_PATH; }

  if (GLBs.OUTPUT_FORMAT != "webp") { return std::nullopt; }

  auto const basePath =
    fs::is_directory(inputPath) ? inputPath : inputPath.parent_path();
  return basePath / "encoded_webp";
}

auto resolveVideoPackOutputPath(fs::path const& inputPath) -> fs::path {
  if (auto const outputPath = resolveVideoOutputPath(inputPath);
      outputPath.has_value()) {
    return outputPath.value() / "packed";
  }

  auto const basePath =
    fs::is_directory(inputPath) ? inputPath : inputPath.parent_path();
  return basePath / "packed";
}

auto groupEncodedVideosForPack(std::vector<fs::path> const& filePaths)
  -> std::vector<std::vector<fs::path>> {
  constexpr auto kMaxZipSize = std::uintmax_t{500 * 1024 * 1024};
  return groupFilesBySize(filePaths, kMaxZipSize);
}

auto splitIntoBatches(std::size_t total, std::size_t batchSize)
  -> std::vector<std::pair<std::size_t, std::size_t>> {
  auto batches = std::vector<std::pair<std::size_t, std::size_t>>{};
  if (total == 0 || batchSize == 0) { return batches; }

  for (auto start = std::size_t{0}; start < total; start += batchSize) {
    auto const end = std::min(start + batchSize, total);
    batches.emplace_back(start, end);
  }

  return batches;
}

bool encodeToHevc(
  fs::path const& inputVidPath,
  std::function<void(std::string const&)> const& statusUpdater
) {
  auto const progressFilePath =
    fs::temp_directory_path() / std::format("progress_{}.txt", getUUID());

  GLBs.PROGRESS_FILES[inputVidPath] = progressFilePath;

  auto const outputPath = resolveVideoOutputPath(GLBs.INPUT_PATH);
  if (outputPath.has_value()) { fs::create_directories(outputPath.value()); }

  auto const cfg = EncodeConfig{
    .ffmpegPath = GLBs.FFMPEG_PATH,
    .inputPath = inputVidPath,
    .outputPath = outputPath,
    .outputFormat = GLBs.OUTPUT_FORMAT,
    .progressFilePath = progressFilePath
  };

  auto const validationResult = cfg.validate();
  if (!validationResult) {
    spdlog::error(validationResult.error());
    return false;
  }

  spdlog::debug("Encoding video: {}", inputVidPath.string());

  if (GLBs.OUTPUT_FORMAT == "webp") {
    return encodeWebpWithTargetSize(
      WebpEncodeContext{
        .inputVidPath = inputVidPath,
        .outputPath = outputPath,
        .progressFilePath = progressFilePath,
        .statusUpdater = statusUpdater
      }
    );
  }

  auto const [exitCode, _] = exec2(cfg.buildCMD());
  return exitCode == 0;
}

int handleSingleFileEncoding(fs::path const& videoPath) {
  return handlePathEncoding(videoPath);
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
    if (auto const& res = bp::parse(line, frameParser); res.has_value()) {
      auto [_, _frameCount] = res.value();
      frameCount = _frameCount;
    }
    if (auto const& res = bp::parse(line, progressParser); res.has_value()) {
      auto [_, _progressStatus] = res.value();
      progressStatus = _progressStatus;
    }
  }

  return {frameCount, progressStatus};
}

auto monitorEncodingProgress(
  progress::ProgressContext& progressCtx,
  fs::path const& vidPath,
  std::size_t barIndex
) -> std::optional<float> {
  auto const totalFrames = getVidTotalFrames(vidPath);
  if (!totalFrames.has_value()) {
    spdlog::error("Failed to get total frames for video: {}", vidPath.string());
    return std::nullopt;
  }

  auto const progressFilePath = GLBs.PROGRESS_FILES[vidPath];
  auto const progressData = tryReadProgressData(progressFilePath);
  if (!progressData.has_value()) { return std::nullopt; }

  auto const frameCount = progressData->frameCount;
  float const progressPercent = ((float)frameCount / totalFrames.value()) * 100.0f;
  progressCtx.setProgress(barIndex, progressPercent);
  return progressPercent;
}

int handlePathEncoding(fs::path const& inputPath) {
  auto const vids = scanInputVideos(inputPath);

  if (vids.empty()) {
    printNoEncodableVideosMessage(inputPath);
    return 0;
  }

  auto const runRes = runEncodingBatches(vids);
  if (!runRes.has_value()) { return 0; }

  auto const& vidsRunRes = runRes.value();

  if (auto const packRes = maybePackOutputs(vidsRunRes); packRes != 0) {
    return packRes;
  }

  printEncodingSummary(vids, vidsRunRes);

  return 0;
}
