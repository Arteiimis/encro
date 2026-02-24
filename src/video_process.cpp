#include "video_process.h"

#include "encode_config.h"
#include "globals.h"
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

bool encodeToHevc(const fs::path& inputVidPath) {
  const auto progressFilePath = fs::temp_directory_path()
                              / std::format("progress_{}.txt", getUUID());

  GLBs.PROGRESS_FILES[inputVidPath] = progressFilePath;

  const auto cfg = EncodeConfig{
    .ffmpegPath = GLBs.FFMPEG_PATH,
    .inputPath = inputVidPath,
    .progressFilePath = progressFilePath
  };

  auto const validationResult = cfg.validate();
  if (!validationResult) {
    spdlog::error(validationResult.error());
    return false;
  }

  spdlog::debug("Executing command: {}", cfg.buildCMD());

  std::println("Encoding video: {}", inputVidPath.string());

  const auto [exitCode, _] = exec2(cfg.buildCMD());

  return exitCode == 0;
}

int handleSingleFileEncoding(const fs::path& videoPath) {
  if (isHevcEncoded(videoPath)) {
    std::println("Video is already HEVC encoded: {}", videoPath.string());
    return 0;
  }

  std::println("Found video file: {}", videoPath.string());

  const auto proceed = readUserIpt(
    "do you want to encode the video to HEVC format? (y/N): "
  );
  if (!proceed) {
    std::println("Encoding task canceled by user.");
    return 0;
  }

  auto returnCode = 0;
  auto pool = BS::pause_thread_pool{2};
  auto progressBar = getProgressBar(
    std::format("Encoding: {}", videoPath.filename().string())
  );

  pool.pause();
  pool.detach_task([&videoPath, &returnCode]() {
    returnCode = encodeToHevc(videoPath);
  });

  using namespace std::chrono_literals;

  pool.detach_task([&progressBar, &videoPath] {
    const auto totalFrames = getVidTotalFrames(videoPath);
    if (!totalFrames.has_value()) {
      spdlog::error("Failed to get total frames for video: {}", videoPath.string());
      return;
    }

    while (true) {
      const auto progressFilePath = GLBs.PROGRESS_FILES[videoPath];

      if (!fs::exists(progressFilePath)) {
        std::this_thread::sleep_for(500ms);
        continue;
      }

      const auto [currentFrame, status] = parseProgressFile(progressFilePath);
      const float progressPercent = ((float)currentFrame / totalFrames.value())
                                  * 100.0;

      spdlog::debug(
        "Video: {}, Frame: {}, Total: {}, Progress: {:.2f}%",
        videoPath.string(),
        currentFrame,
        totalFrames.value(),
        progressPercent
      );

      progressBar->set_progress(progressPercent);

      if (currentFrame >= totalFrames.value() || status == "end") { break; }
      std::this_thread::sleep_for(500ms);
    }
  });

  pool.unpause();
  pool.wait();

  return returnCode;
}

auto readLastNLines(const fs::path& filePath, std::size_t n)
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

auto parseProgressFile(const fs::path& progressFilePath) -> ProgressData {
  namespace bp = boost::parser;

  const auto lines = readLastNLines(progressFilePath, 12);
  auto frameCount = uint64_t{0};
  auto progressStatus = std::string{};

  const auto frameParser = bp::string("frame=") >> bp::uint_;
  const auto progressParser = bp::string("progress=") >> *bp::char_;

  for (const auto& line: lines) {
    if (const auto& res = bp::parse(line, frameParser); res.has_value()) {
      auto [_, _frameCount] = res.value();
      frameCount = _frameCount;
    }
    if (const auto& res = bp::parse(line, progressParser); res.has_value()) {
      auto [_, _progressStatus] = res.value();
      progressStatus = _progressStatus;
    }
  }

  return {frameCount, progressStatus};
}

auto monitorEncodingProgress(
  indicators::DynamicProgress<indicators::ProgressBar>& progressManager,
  const std::unordered_map<fs::path, std::size_t>& progressBarIndexs,
  const fs::path& vidPath
) -> void {
  using namespace std::chrono_literals;

  const auto totalFrames = getVidTotalFrames(vidPath);
  if (!totalFrames.has_value()) {
    spdlog::error("Failed to get total frames for video: {}", vidPath.string());
    return;
  }

  while (true) {
    const auto progressFilePath = GLBs.PROGRESS_FILES[vidPath];

    if (!fs::exists(progressFilePath)) {
      std::this_thread::sleep_for(500ms);
      continue;
    }

    const auto [frameCount, status] = parseProgressFile(progressFilePath);
    const float progressPercent = ((float)frameCount / totalFrames.value()) * 100.0;

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

int handlePathEncoding(const fs::path& inputPath) {
  namespace rng = std::ranges;

  const auto vids = readAllVids(inputPath);

  auto vidsRunRes = std::unordered_map<fs::path, bool>{};
  auto pool = BS::pause_thread_pool{vids.size() * 2};
  auto bars = std::vector<std::unique_ptr<indicators::ProgressBar>>{};
  auto progressManager = indicators::DynamicProgress<indicators::ProgressBar>{};
  auto progressBarIndexs = std::unordered_map<fs::path, std::size_t>{};

  pool.pause();
  for (const auto& vidPath: vids) {
    // Initialize progress bar for this video
    bars.emplace_back(
      getProgressBar(std::format("Encoding: {}", vidPath.filename().string()))
    );
    progressBarIndexs[vidPath] = progressManager.push_back(*bars.back());

    // Schedule encoding task
    pool.detach_task([&vidsRunRes, vidPath]() {
      vidsRunRes[vidPath] = encodeToHevc(vidPath);
    });
    pool.detach_task([&progressManager, &progressBarIndexs, vidPath] {
      monitorEncodingProgress(progressManager, progressBarIndexs, vidPath);
    });
  }

  std::println("found {} video(s) in directory: {}", vids.size(), inputPath.string());
  const auto proceed = readUserIpt(
    "do you want to encode the video to HEVC format? (y/N): "
  );
  if (!proceed) {
    std::println("Encoding tasks canceled by user.");
    return 0;
  }

  cursorToggleVisibility(false);
  pool.unpause();
  pool.wait();
  cursorToggleVisibility(true);

  std::println("All encoding tasks completed.");
  std::println("Summary:");
  std::println("\tTotal videos found: {}", rng::distance(vids));

  const auto successCount = rng::count_if(vidsRunRes, _1->*second);
  const auto failureCount = vidsRunRes.size() - successCount;

  std::println("\tSuccessfully encoded: {}", successCount);
  std::println("\tFailed to encode: {}", failureCount);

  if (failureCount > 0) {
    std::println("Videos that failed to encode:");
    for (const auto& [vidPath, success]: vidsRunRes) {
      if (!success) { std::println("\t{}", vidPath.string()); }
    }
  }

  return 0;
}
