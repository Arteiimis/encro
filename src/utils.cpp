#include <print>
#include <iostream>

#include <boost/uuid.hpp>
#include <boost/lexical_cast.hpp>
#include <spdlog/spdlog.h>

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
  if (GLBs.YES_TO_ALL) { return true; }

  std::print("do you want to encode the video to HEVC format? (y/N): ");

  auto response = 'n';
  auto input    = std::string{};
  std::getline(std::cin, input);
  if (!input.empty()) { std::istringstream(input) >> response; }

  return response == 'y' || response == 'Y';
}

auto findFFprobe() -> std::optional<fs::path> {
  if (!GLBs.FFMPEG_INSTALL_DIR.has_value() && exec2("ffprobe -version").first == 0) {
    return fs::path{"ffprobe"};
  }

  if (fs::is_regular_file(GLBs.FFMPEG_INSTALL_DIR.value())
      && GLBs.FFMPEG_INSTALL_DIR.value().filename() == "ffprobe") {
    const auto cmd = std::format(
      "\"{}\" -version",
      GLBs.FFMPEG_INSTALL_DIR.value().string()
    );

    if (exec2(cmd).first == 0) { return GLBs.FFMPEG_INSTALL_DIR; }
  }

  if (fs::is_directory(GLBs.FFMPEG_INSTALL_DIR.value())) {
    auto path = GLBs.FFMPEG_INSTALL_DIR.value() / "ffprobe";
    auto cmd  = std::format("\"{}\" -version", path.string());

    if (exec2(cmd).first == 0) { return path; }
  }

  return std::nullopt;
}

auto findFFmpeg() -> std::optional<fs::path> {
  if (!GLBs.FFMPEG_INSTALL_DIR.has_value() && exec2("ffmpeg -version").first == 0) {
    return fs::path{"ffmpeg"};
  }

  if (fs::is_regular_file(GLBs.FFMPEG_INSTALL_DIR.value())
      && GLBs.FFMPEG_INSTALL_DIR.value().filename() == "ffmpeg") {
    auto cmd = std::format(
      "\"{}\" -version",
      GLBs.FFMPEG_INSTALL_DIR.value().string()
    );

    if (exec2(cmd).first == 0) { return GLBs.FFMPEG_INSTALL_DIR; }
  }

  if (fs::is_directory(GLBs.FFMPEG_INSTALL_DIR.value())) {
    auto path = GLBs.FFMPEG_INSTALL_DIR.value() / "ffmpeg";
    auto cmd  = std::format("\"{}\" -version", path.string());

    if (exec2(cmd).first == 0) { return path; }
  }

  return std::nullopt;
}

bool toolCheck() {
  if (GLBs.FFMPEG_PATH = findFFmpeg(); !GLBs.FFMPEG_PATH.has_value()) {
    spdlog::error(
      "FFmpeg not found. Please ensure FFmpeg is installed and accessible."
    );
    return false;
  }

  if (GLBs.FFPROBE_PATH = findFFprobe(); !GLBs.FFPROBE_PATH.has_value()) {
    spdlog::error(
      "FFprobe not found. Please ensure FFprobe is installed and accessible."
    );
    return false;
  }

  spdlog::debug("Using FFmpeg at: {}", GLBs.FFMPEG_PATH.value().string());
  spdlog::debug("Using FFprobe at: {}", GLBs.FFPROBE_PATH.value().string());

  return true;
}

std::string getUUID() {
  return boost::lexical_cast<std::string>(boost::uuids::random_generator{}());
}
