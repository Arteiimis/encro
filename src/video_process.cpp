#include "video_process.h"

#include "encode_config.h"
#include "globals.h"
#include "packer.h"
#include "progress.h"
#include "utils.h"
#include "video_info.h"

#include <BS_thread_pool.hpp>
#include <boost/lambda2.hpp>
#include <boost/parser/parser.hpp>
#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>
#include <spdlog/spdlog.h>

#include <deque>
#include <fstream>
#include <print>
#include <ranges>

namespace fs = std::filesystem;
using namespace boost::lambda2;
using namespace indicators;

auto monitorEncodingProgress(
  indicators::DynamicProgress<indicators::ProgressBar>& progressManager,
  std::unordered_map<fs::path, std::size_t> const& progressBarIndexs,
  fs::path const& vidPath
) -> void;

namespace {

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
  constexpr auto kBatchSize = std::size_t{10};

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

  auto const batches = splitIntoBatches(vids.size(), kBatchSize);
  auto const totalBatchCount = batches.size();

  for (auto batchIndex = std::size_t{0}; batchIndex < totalBatchCount;
       ++batchIndex) {
    auto const [start, end] = batches[batchIndex];
    auto const batchSize = end - start;

    std::println(
      "Processing batch {}/{} ({} video(s))...",
      batchIndex + 1,
      totalBatchCount,
      batchSize
    );

    auto pool = BS::pause_thread_pool{std::min(batchSize, kBatchSize) * 2};
    auto bars = std::vector<std::unique_ptr<indicators::ProgressBar>>{};
    auto progressManager = indicators::DynamicProgress<indicators::ProgressBar>{};
    auto progressBarIndexs = std::unordered_map<fs::path, std::size_t>{};

    pool.pause();

    for (auto i = start; i < end; ++i) {
      auto const& vidPath = vids[i];

      progressBarIndexs[vidPath] = progress::addBar(
        progressManager,
        bars,
        std::format("Encoding: {}", vidPath.filename().string())
      );

      pool.detach_task([&vidsRunRes, &vidsRunResMtx, vidPath] {
        auto const result = encodeToHevc(vidPath);
        auto _ = std::scoped_lock{vidsRunResMtx};
        vidsRunRes[vidPath] = result;
      });

      pool.detach_task([&progressManager, &progressBarIndexs, vidPath] {
        monitorEncodingProgress(progressManager, progressBarIndexs, vidPath);
      });
    }

    pool.unpause();
    pool.wait();
  }

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

  auto bars = std::vector<std::unique_ptr<indicators::ProgressBar>>{};
  auto progressManager = indicators::DynamicProgress<indicators::ProgressBar>{};

  std::println(
    "Packing {} encoded video(s) into {} archive(s)...",
    encodedOutputFiles.size(),
    groupedFiles.size()
  );

  for (auto const& [index, group]: std::views::enumerate(groupedFiles)) {
    auto const zipPath =
      zipOutputDir / std::format("encoded_videos_part{}.zip", index + 1);

    auto const barIndex = progress::addBar(
      progressManager,
      bars,
      std::format("Packing: {}", zipPath.filename().string())
    );

    if (auto const packRes =
          packFilesToZip(group, zipPath, progressManager, barIndex);
        !packRes) {
      spdlog::error("Failed to pack encoded videos: {}", packRes.error());
      return 1;
    }

    std::println("Packed archive: {}", zipPath.string());
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
  indicators::DynamicProgress<indicators::ProgressBar>& progressManager,
  std::unordered_map<fs::path, std::size_t> const& progressBarIndexs,
  fs::path const& vidPath
) -> void {
  using namespace std::chrono_literals;

  auto const totalFrames = getVidTotalFrames(vidPath);
  if (!totalFrames.has_value()) {
    spdlog::error("Failed to get total frames for video: {}", vidPath.string());
    return;
  }

  while (true) {
    auto const progressFilePath = GLBs.PROGRESS_FILES[vidPath];

    if (!fs::exists(progressFilePath)) {
      std::this_thread::sleep_for(500ms);
      continue;
    }

    auto const [frameCount, status] = parseProgressFile(progressFilePath);
    float const progressPercent = ((float)frameCount / totalFrames.value()) * 100.0;

    spdlog::debug(
      "Video: {}, Frame: {}, Total: {}, Progress: {:.2f}%",
      vidPath.string(),
      frameCount,
      totalFrames.value(),
      progressPercent
    );

    progressManager[progressBarIndexs.at(vidPath)].set_progress(progressPercent);

    if (frameCount >= totalFrames.value() || status == "end") { break; }

    std::this_thread::sleep_for(500ms);
  }
}

int handlePathEncoding(fs::path const& inputPath) {
  auto const vids = readAllVids(inputPath);

  if (vids.empty()) {
    printNoEncodableVideosMessage(inputPath);
    return 0;
  }

  std::println(
    "found {} video(s) in directory: {}",
    vids.size(),
    inputPath.string()
  );

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
