#include <iostream>
#include <print>

#include <boost/uuid.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/process/v1.hpp>
#include <spdlog/spdlog.h>

#include "core/globals.h"
#include "utils/utils.h"

auto exec2(std::string_view cmd) -> ExecResult {
  namespace bp = boost::process::v1;

  spdlog::debug("Executing command: {}", cmd);

  auto pipeStream = bp::ipstream{};
  auto process = bp::child(cmd.data(), bp::std_out > pipeStream);
  auto line = std::string{};
  auto result = std::string{};

  while (std::getline(pipeStream, line)) {
    std::format_to(std::back_inserter(result), "{}\n", line);
  }
  process.wait();

  return {process.exit_code(), result};
}

bool readUserIpt(std::string_view prompt) {
  if (GLBs.YES_TO_ALL) { return true; }

  if (!prompt.empty()) { std::print("{}", prompt); }

  auto response = 'n';
  auto input = std::string{};
  std::getline(std::cin, input);
  if (!input.empty()) { std::istringstream(input) >> response; }

  return response == 'y' || response == 'Y';
}

auto findFFprobe(std::optional<fs::path> const& installDir)
  -> std::optional<fs::path> {
  const auto systemFFprobeAvailable = exec2("ffprobe -version").exitCode == 0;

  if (!installDir.has_value() && systemFFprobeAvailable) {
    return fs::path{"ffprobe"};
  }

  if (!installDir.has_value() || !fs::is_directory(installDir.value())) {
    return std::nullopt;
  }

  auto pathIter = fs::recursive_directory_iterator{installDir.value()};

  for (const auto& entry: pathIter) {
    if (entry.is_regular_file() && entry.path().filename() == "ffprobe") {
      auto cmd = std::format("\"{}\" -version", entry.path().string());
      if (exec2(cmd).exitCode == 0) { return entry.path(); }
    }
  }

  return std::nullopt;
}

auto findFFprobe() -> std::optional<fs::path> {
  return findFFprobe(GLBs.FFMPEG_INSTALL_DIR);
}

auto findFFmpeg(std::optional<fs::path> const& installDir)
  -> std::optional<fs::path> {
  const auto systemFFmpegAvailable = exec2("ffmpeg -version").exitCode == 0;

  if (!installDir.has_value() && systemFFmpegAvailable) {
    return fs::path{"ffmpeg"};
  }

  if (!installDir.has_value() || !fs::is_directory(installDir.value())) {
    return std::nullopt;
  }

  auto pathIter = fs::recursive_directory_iterator{installDir.value()};

  for (const auto& entry: pathIter) {
    if (entry.is_regular_file() && entry.path().filename() == "ffmpeg") {
      auto cmd = std::format("\"{}\" -version", entry.path().string());
      if (exec2(cmd).exitCode == 0) { return entry.path(); }
    }
  }

  return std::nullopt;
}

auto findFFmpeg() -> std::optional<fs::path> {
  return findFFmpeg(GLBs.FFMPEG_INSTALL_DIR);
}

auto find7zip() -> std::optional<fs::path> {
  if (exec2("7z").exitCode == 0) { return fs::path{"7z"}; }

  return std::nullopt;
}

auto toolCheck() -> eh::Result<void> {
  if (GLBs.FFMPEG_PATH = findFFmpeg(); !GLBs.FFMPEG_PATH.has_value()) {
    return eh::makeError(
      "FFmpeg not found. Please ensure FFmpeg is installed and accessible."
    );
  }

  if (GLBs.FFPROBE_PATH = findFFprobe(); !GLBs.FFPROBE_PATH.has_value()) {
    return eh::makeError(
      "FFprobe not found. Please ensure FFprobe is installed and accessible."
    );
  }

  spdlog::info("Using FFmpeg at: {}", GLBs.FFMPEG_PATH.value().string());
  spdlog::info("Using FFprobe at: {}", GLBs.FFPROBE_PATH.value().string());

  return {};
}

std::string getUUID() {
  return boost::lexical_cast<std::string>(boost::uuids::random_generator{}());
}

auto getParamStr(
  const boost::program_options::variables_map& vm,
  std::string_view paramName
) -> std::string {
  return boost::trim_copy(vm.at(paramName.data()).as<std::string>());
}
