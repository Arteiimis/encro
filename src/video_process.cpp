#include "video_process.h"

#include "encode_config.h"
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


namespace fs = std::filesystem;
using namespace boost::lambda2;
using namespace indicators;

auto monitorEncodingProgress(
  progress::ProgressContext& progressCtx,
  fs::path const& vidPath,
  std::size_t barIndex
) -> void;

namespace {

auto truncateForProgressLabel(
  std::string const& text,
  std::size_t maxLen = 48
) -> std::string {
  if (text.size() <= maxLen) { return text; }
  if (maxLen <= 3) { return text.substr(0, maxLen); }
  return std::format("{}...", text.substr(0, maxLen - 3));
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

auto runEncodingBatches(std::vector<fs::path> const& vids)
  -> std::optional<std::unordered_map<fs::path, bool>> {
  auto vidsRunRes = std::unordered_map<fs::path, bool>{};
  auto vidsRunResMtx = std::mutex{};
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

  auto progressCtx = progress::ProgressContext{};
  auto finishedCount = std::atomic_size_t{0};
  auto nextTaskIndex = std::atomic_size_t{0};

  auto const workerCount = std::min(vids.size(), kMaxConcurrentJobs);
  auto slotTaskPaths = std::vector<std::optional<fs::path>>(workerCount);
  auto slotTaskPathsMtx = std::mutex{};
  auto slotBarIndexes = std::vector<std::size_t>(workerCount);

  std::println(
    "Scheduling {} video(s) with max {} concurrent encode job(s)...",
    vids.size(),
    workerCount
  );

  for (auto slot = std::size_t{0}; slot < workerCount; ++slot) {
    slotBarIndexes[slot] =
      progressCtx.addBar(std::format("Encoding: [idle-{}]", slot + 1));
  }

  auto monitorThread = std::jthread([&] {
    using namespace std::chrono_literals;

    while (finishedCount.load(std::memory_order_acquire) < vids.size()) {
      auto activeBars = std::vector<std::pair<fs::path, std::size_t>>{};
      {
        auto lock = std::scoped_lock{slotTaskPathsMtx};
        activeBars.reserve(slotTaskPaths.size());
        for (auto slot = std::size_t{0}; slot < slotTaskPaths.size(); ++slot) {
          if (slotTaskPaths[slot].has_value()) {
            activeBars.emplace_back(
              slotTaskPaths[slot].value(),
              slotBarIndexes[slot]
            );
          }
        }
      }

      for (auto const& [vidPath, barIndex]: activeBars) {
        monitorEncodingProgress(progressCtx, vidPath, barIndex);
      }

      std::this_thread::sleep_for(100ms);
    }
  });

  parallel::runIndexedTasks(workerCount, workerCount, [&](std::size_t slot) {
    auto const barIndex = slotBarIndexes[slot];

    while (true) {
      auto const taskIndex = nextTaskIndex.fetch_add(1, std::memory_order_acq_rel);
      if (taskIndex >= vids.size()) { break; }

      auto const& vidPath = vids[taskIndex];

      {
        auto lock = std::scoped_lock{slotTaskPathsMtx};
        slotTaskPaths[slot] = vidPath;
      }

        progressCtx.setPostfixText(
          barIndex,
          std::format(
            "Encoding: {}",
            truncateForProgressLabel(vidPath.filename().string())
          )
        );
      progressCtx.setProgress(barIndex, 0.0f);
      auto const result = encodeToHevc(vidPath);

      {
        auto _ = std::scoped_lock{vidsRunResMtx};
        vidsRunRes[vidPath] = result;
      }

      progressCtx.setProgress(barIndex, 100.0f);
        progressCtx.setPostfixText(
          barIndex,
          std::format("Encoding: [idle-{}]", slot + 1)
        );

      {
        auto lock = std::scoped_lock{slotTaskPathsMtx};
        slotTaskPaths[slot].reset();
      }

      finishedCount.fetch_add(1, std::memory_order_release);
    }
  });

  monitorThread.join();

  return vidsRunRes;
}

auto collectEncodedOutputFiles(std::unordered_map<fs::path, bool> const& vidsRunRes)
  -> std::vector<fs::path> {
  auto encodedOutputFiles = std::vector<fs::path>{};
  encodedOutputFiles.reserve(vidsRunRes.size());

  auto const outputPath = resolveVideoOutputPath(GLBs.INPUT_PATH);
  for (auto const& [vidPath, success]: vidsRunRes) {
    if (!success) { continue; }

    auto const cfg = EncodeConfig{
      .ffmpegPath = GLBs.FFMPEG_PATH,
      .inputPath = vidPath,
      .outputPath = outputPath,
      .outputFormat = GLBs.OUTPUT_FORMAT
    };

    auto const outFile = cfg.buildOutputPath();
    if (fs::exists(outFile)) { encodedOutputFiles.emplace_back(outFile); }
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

bool encodeToHevc(fs::path const& inputVidPath) {
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

  spdlog::debug("Executing command: {}", cfg.buildCMD());

  spdlog::debug("Encoding video: {}", inputVidPath.string());

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
) -> void {
  auto const totalFrames = getVidTotalFrames(vidPath);
  if (!totalFrames.has_value()) {
    spdlog::error("Failed to get total frames for video: {}", vidPath.string());
    return;
  }

  auto const progressFilePath = GLBs.PROGRESS_FILES[vidPath];
  if (!fs::exists(progressFilePath)) { return; }

  auto const [frameCount, _status] = parseProgressFile(progressFilePath);
  float const progressPercent = ((float)frameCount / totalFrames.value()) * 100.0f;

  spdlog::debug(
    "Video: {}, Frame: {}, Total: {}, Progress: {:.2f}%",
    vidPath.string(),
    frameCount,
    totalFrames.value(),
    progressPercent
  );

  progressCtx.setProgress(barIndex, progressPercent);
}

int handlePathEncoding(fs::path const& inputPath) {
  std::println("Scanning input path for videos: {} ...", inputPath.string());
  auto const vids = readAllVids(inputPath);
  std::println("Video scan completed, found {} candidate file(s).", vids.size());

  if (vids.empty()) {
    printNoEncodableVideosMessage(inputPath);
    return 0;
  }

  auto const runRes = runEncodingBatches(vids);
  if (!runRes.has_value()) { return 0; }

  auto const& vidsRunRes = runRes.value();

  if (GLBs.PACK_OUTPUT) {
    if (auto const packRes = packEncodedVideos(vidsRunRes); packRes != 0) {
      return packRes;
    }
  }

  printEncodingSummary(vids, vidsRunRes);

  return 0;
}
