#include <print>
#include <fstream>
#include <ranges>

#include <BS_thread_pool.hpp>
#include <boost/lambda2.hpp>
#include <boost/parser/parser.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/dynamic_progress.hpp>
#include <spdlog/spdlog.h>

#include "video_process.h"
#include "video_info.h"
#include "globals.h"
#include "utils.h"

namespace fs = std::filesystem;
using namespace boost::lambda2;
using namespace indicators;

bool encodeToHevc(const fs::path& inputVidPath) {
  const auto outputVidDir = GLBs.OUTPUT_PATH.value_or(inputVidPath.parent_path());
  const auto outputVidPath = outputVidDir
                           / std::format("{}.hevc.mp4", inputVidPath.stem().string());
  const auto progressFilePath = fs::temp_directory_path()
                              / std::format("progress_{}.txt", getUUID());

  GLBs.PROGRESS_FILES[inputVidPath] = progressFilePath;

  const auto cmd = std::format(
    "{} -i \"{}\" -c:v hevc_nvenc -crf 20 \"{}\" -progress \"{}\"",
    GLBs.FFMPEG_PATH.value().string(),
    inputVidPath.string(),
    outputVidPath.string(),
    progressFilePath.string()
  );

  spdlog::debug("Executing command: {}", cmd);

  std::println("Encoding video: {}", inputVidPath.string());

  const auto [exitCode, output] = exec2(cmd);

  return exitCode == 0;
}

int handleSingleFileEncoding(const fs::path& videoPath) {
  if (isHevcEncoded(videoPath)) {
    std::println("Video is already HEVC encoded: {}", videoPath.string());
    return 0;
  }

  std::println("Found video file: {}", videoPath.string());

  if (const auto proceed = readUserIpt(); !proceed) {
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
    while (true) {
      const auto progressFilePath = GLBs.PROGRESS_FILES[videoPath];

      if (!fs::exists(progressFilePath)) {
        std::this_thread::sleep_for(500ms);
        continue;
      }

      const auto [currentFrame, status] = parseProgressFile(progressFilePath);
      const auto totalFrames = getVidTotalFrames(videoPath);
      const float progressPercent = ((float)currentFrame / totalFrames) * 100.0;

      spdlog::debug(
        "Video: {}, Frame: {}, Total: {}, Progress: {:.2f}%",
        videoPath.string(),
        currentFrame,
        totalFrames,
        progressPercent
      );

      progressBar->set_progress(progressPercent);

      if (currentFrame >= totalFrames || status == "end") { break; }
      std::this_thread::sleep_for(500ms);
    }
  });

  pool.unpause();
  pool.wait();

  return returnCode;
}

auto readLastNLines(const fs::path& filePath, std::size_t n)
  -> std::vector<std::string> {
  auto file = std::ifstream(filePath.string(), std::ios::ate);
  auto lines = std::vector<std::string>{};
  lines.reserve(n);

  if (!file.is_open()) { return lines; }

  auto fileSize = file.tellg();
  auto buffer = std::vector<char>(fileSize);

  file.seekg(0, std::ios::beg);
  file.read(buffer.data(), fileSize);

  auto count = 0;
  auto it = buffer.rbegin();

  while (it != buffer.rend() && count < n) {
    if (*it == '\n' && count > 0) { ++count; }
    ++it;
  }

  auto lastPart = std::string(it.base(), buffer.end());
  auto stream = std::istringstream(lastPart);
  auto line = std::string{};

  while (std::getline(stream, line)) { lines.push_back(line); }

  if (lines.size() > n) { lines.erase(lines.begin(), lines.end() - n); }

  return lines;
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
  while (true) {
    auto progressFilePath = fs::path{};
    try {
      progressFilePath = GLBs.PROGRESS_FILES.at(vidPath);
    } catch (...) {
      std::this_thread::sleep_for(500ms);
      continue;
    }

    const auto [frameCount, status] = parseProgressFile(progressFilePath);
    const float progressPercent = ((float)frameCount / totalFrames) * 100.0;

    spdlog::debug(
      "Video: {}, Frame: {}, Total: {}, Progress: {:.2f}%",
      vidPath.string(),
      frameCount,
      totalFrames,
      progressPercent
    );

    progressManager[progressBarIndexs.at(vidPath)].set_progress(progressPercent);

    if (frameCount >= totalFrames || status == "end") { break; }

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

  if (const auto proceed = readUserIpt(); !proceed) {
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
