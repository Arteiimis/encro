#include <iostream>

#include <boost/uuid.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/process/v1.hpp>
#include <spdlog/spdlog.h>

#include "globals.h"
#include "utils.h"

auto exec2(std::string_view cmd) -> ExecResult {
  namespace bp = boost::process::v1;

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

bool readUserIpt() {
  if (GLBs.YES_TO_ALL) { return true; }

  auto response = 'n';
  auto input = std::string{};
  std::getline(std::cin, input);
  if (!input.empty()) { std::istringstream(input) >> response; }

  return response == 'y' || response == 'Y';
}

auto findFFprobe() -> std::optional<fs::path> {
  const auto hasInstallDir = GLBs.FFMPEG_INSTALL_DIR.has_value();
  const auto systemFFprobeAvailable = exec2("ffprobe -version").exitCode == 0;

  if (!hasInstallDir && systemFFprobeAvailable) { return fs::path{"ffprobe"}; }

  auto pathIter = fs::recursive_directory_iterator{GLBs.FFMPEG_INSTALL_DIR.value()};

  for (const auto& entry: pathIter) {
    if (entry.is_regular_file() && entry.path().filename() == "ffprobe") {
      auto cmd = std::format("\"{}\" -version", entry.path().string());
      if (exec2(cmd).exitCode == 0) { return entry.path(); }
    }
  }

  return std::nullopt;
}

auto findFFmpeg() -> std::optional<fs::path> {
  const auto hasInstallDir = GLBs.FFMPEG_INSTALL_DIR.has_value();
  const auto systemFFmpegAvailable = exec2("ffmpeg -version").exitCode == 0;

  if (!hasInstallDir && systemFFmpegAvailable) { return fs::path{"ffmpeg"}; }

  auto pathIter = fs::recursive_directory_iterator{GLBs.FFMPEG_INSTALL_DIR.value()};

  for (const auto& entry: pathIter) {
    if (entry.is_regular_file() && entry.path().filename() == "ffmpeg") {
      auto cmd = std::format("\"{}\" -version", entry.path().string());
      if (exec2(cmd).exitCode == 0) { return entry.path(); }
    }
  }

  return std::nullopt;
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

auto getProgressBar(std::string_view promptText)
  -> std::unique_ptr<indicators::ProgressBar> {
  using namespace indicators;
  return std::make_unique<ProgressBar>(
    option::BarWidth{50},
    option::Start{"["},
    option::End{"]"},
    option::PostfixText{promptText},
    option::ForegroundColor{Color::white},
    option::ShowElapsedTime{true},
    option::ShowRemainingTime{true},
    option::MaxProgress{100}
  );
}

#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h>
#endif

void cursorToggleVisibility(bool visible) {
#if defined(_WIN32) || defined(_WIN64)
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_CURSOR_INFO cursorInfo;

  GetConsoleCursorInfo(hConsole, &cursorInfo);
  cursorInfo.bVisible = visible;
  SetConsoleCursorInfo(hConsole, &cursorInfo);
#else
  if (visible) {
    std::print("\033[?25h");
  } else {
    std::print("\033[?25l");
  }
#endif
}

auto getParamStr(
  const boost::program_options::variables_map& vm,
  std::string_view paramName
) -> std::string {
  return boost::trim_copy(vm.at(paramName.data()).as<std::string>());
}
