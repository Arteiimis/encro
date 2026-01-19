#include <print>
#include <fstream>
#include <ranges>

#include <BS_thread_pool.hpp>
#include <boost/lambda2.hpp>
#include <boost/parser/parser.hpp>
#include <indicators/progress_bar.hpp>

#include "video_process.h"
#include "video_info.h"
#include "globals.h"
#include "utils.h"

namespace fs = std::filesystem;
using namespace boost::lambda2;

bool encodeToHevc(const fs::path& inputVidPath) {
  const auto outputVidDir     = OUTPUT_PATH.value_or(inputVidPath.parent_path());
  const auto outputVidPath    = outputVidDir / inputVidPath.filename();
  const auto progressFilePath = fs::temp_directory_path()
                              / std::format("progress_{}.txt", UUID_GENERATOR());

  PROGRESS_FILES[inputVidPath] = progressFilePath;

  const auto cmd = std::format(
    "{} -i \"{}\" -c:v hevc_nvenc -crf 20 \"{}\" -progress \"{}\"",
    FFMPEG_PATH.value().string(),
    inputVidPath.string(),
    outputVidPath.string(),
    progressFilePath.string()
  );

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

  if (encodeToHevc(videoPath)) {
    std::println("Successfully encoded: {}", videoPath.string());
  } else {
    std::println("Failed to encode: {}", videoPath.string());
  }

  return 0;
}

auto getProgressBar(const fs::path& videoPath) -> indicators::ProgressBar {
  using namespace indicators;
  return ProgressBar{
    option::BarWidth{50},
    option::Start{"["},
    option::End{"]"},
    option::PrefixText{std::format("Encoding: {}", videoPath.filename().string())},
    option::ForegroundColor{Color::white},
    option::ShowElapsedTime{true},
    option::ShowRemainingTime{true},
    option::MaxProgress{100}
  };
}

auto readLastNLines(const fs::path& filePath, std::size_t n)
  -> std::vector<std::string> {
  auto file  = std::ifstream(filePath.string(), std::ios::ate);
  auto lines = std::vector<std::string>{};
  lines.reserve(n);

  if (!file.is_open()) { return lines; }

  auto fileSize = file.tellg();
  auto buffer   = std::vector<char>(fileSize);

  file.seekg(0, std::ios::beg);
  file.read(buffer.data(), fileSize);

  auto count = 0;
  auto it    = buffer.rbegin();

  while (it != buffer.rend() && count < n) {
    if (*it == '\n' && count > 0) { ++count; }
    ++it;
  }

  auto lastPart = std::string(it.base(), buffer.end());
  auto stream   = std::istringstream(lastPart);
  auto line     = std::string{};

  while (std::getline(stream, line)) { lines.push_back(line); }

  if (lines.size() > n) { lines.erase(lines.begin(), lines.end() - n); }

  return lines;
}

auto getFrameCountFromProgress(const fs::path& progressFilePath) -> uint64_t {
  namespace bp = boost::parser;

  const auto lines = readLastNLines(progressFilePath, 12);

  const auto parser = bp::string("frame") >> '=' >> bp::uint_;

  for (const auto& line: lines) {
    if (const auto& res = bp::parse(line, parser); res.has_value()) {
      auto [_, frameCount] = res.value();
      return frameCount;
    }
  }

  return 0;
}

int handlePathEncoding(const fs::path& inputPath) {
  namespace rng = std::ranges;

  const auto vids = readAllVids(inputPath);

  auto vidsRunRes = std::unordered_map<fs::path, bool>{};
  auto pool       = BS::pause_thread_pool{std::thread::hardware_concurrency()};
  pool.pause();

  for (const auto& vidPath: vids) {
    pool.detach_task([&vidsRunRes, vidPath]() {
      if (encodeToHevc(vidPath)) {
        std::println("Successfully encoded: {}", vidPath.string());
        vidsRunRes[vidPath] = true;
      } else {
        std::println("Failed to encode: {}", vidPath.string());
        vidsRunRes[vidPath] = false;
      }
    });
  }

  std::println("found {} video(s) in directory: {}", vids.size(), inputPath.string());

  if (const auto proceed = readUserIpt(); !proceed) {
    std::println("Encoding tasks canceled by user.");
    return 0;
  }

  pool.unpause();

  while (true) { }

  pool.wait();

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
