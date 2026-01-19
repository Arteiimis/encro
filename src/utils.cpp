#include <print>
#include <iostream>

#include "globals.h"
#include "utils.h"

auto exec2(std::string_view cmd) -> std::pair<int, std::string> {
  namespace bp = boost::process::v1;

  auto pipeStream = bp::ipstream{};
  auto process    = bp::child(cmd.data(), bp::std_out > pipeStream);
  auto line       = std::string{};
  auto result     = std::string{};

  while (std::getline(pipeStream, line)) {
    std::format_to(std::back_inserter(result), "{}\n", line);
  }
  process.wait();

  return std::pair{process.exit_code(), result};
}

bool readUserIpt() {
  std::print("do you want to encode the video to HEVC format? (y/N): ");

  auto response = 'n';
  auto input    = std::string{};
  std::getline(std::cin, input);
  if (!input.empty()) { std::istringstream(input) >> response; }

  return response == 'y' || response == 'Y';
}

auto findFFprobe() -> std::optional<fs::path> {
  if (!FFMPEG_INSTALL_DIR.has_value() && exec2("ffprobe -version").first == 0) {
    return fs::path{"ffprobe"};
  }

  if (fs::is_regular_file(FFMPEG_INSTALL_DIR.value())
      && FFMPEG_INSTALL_DIR.value().filename() == "ffprobe") {
    const auto cmd = std::format(
      "\"{}\" -version",
      FFMPEG_INSTALL_DIR.value().string()
    );

    if (exec2(cmd).first == 0) { return FFMPEG_INSTALL_DIR; }
  }

  if (fs::is_directory(FFMPEG_INSTALL_DIR.value())) {
    auto path = FFMPEG_INSTALL_DIR.value() / "ffprobe";
    auto cmd  = std::format("\"{}\" -version", path.string());

    if (exec2(cmd).first == 0) { return path; }
  }

  return std::nullopt;
}

auto findFFmpeg() -> std::optional<fs::path> {
  if (!FFMPEG_INSTALL_DIR.has_value() && exec2("ffmpeg -version").first == 0) {
    return fs::path{"ffmpeg"};
  }

  if (fs::is_regular_file(FFMPEG_INSTALL_DIR.value())
      && FFMPEG_INSTALL_DIR.value().filename() == "ffmpeg") {
    auto cmd = std::format("\"{}\" -version", FFMPEG_INSTALL_DIR.value().string());

    if (exec2(cmd).first == 0) { return FFMPEG_INSTALL_DIR; }
  }

  if (fs::is_directory(FFMPEG_INSTALL_DIR.value())) {
    auto path = FFMPEG_INSTALL_DIR.value() / "ffmpeg";
    auto cmd  = std::format("\"{}\" -version", path.string());

    if (exec2(cmd).first == 0) { return path; }
  }

  return std::nullopt;
}

bool toolCheck() {
  if (FFMPEG_PATH = findFFmpeg(); !FFMPEG_PATH.has_value()) {
    std::println(
      "FFmpeg not found. Please ensure FFmpeg is installed and accessible."
    );
    return false;
  }

  if (FFPROBE_PATH = findFFprobe(); !FFPROBE_PATH.has_value()) {
    std::println(
      "FFprobe not found. Please ensure FFprobe is installed and accessible."
    );
    return false;
  }

  std::println("Using FFmpeg at: {}", FFMPEG_PATH.value().string());
  std::println("Using FFprobe at: {}", FFPROBE_PATH.value().string());

  return true;
}
