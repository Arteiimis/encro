#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace fs = std::filesystem;

namespace {

struct FfmpegInvocation {
  std::optional<fs::path> outputFile;
  std::optional<fs::path> progressFile;
  std::optional<double> seekSeconds;
  std::optional<double> durationSeconds;
};

auto readEnv(std::string const& key) -> std::optional<std::string> {
  auto const* value = std::getenv(key.c_str());
  if (value == nullptr) { return std::nullopt; }
  return std::string{value};
}

auto readEnvInt(std::string const& key, int defaultValue) -> int {
  if (auto const value = readEnv(key); value.has_value()) {
    try {
      return std::stoi(value.value());
    } catch (...) { }
  }
  return defaultValue;
}

auto readEnvSize(std::string const& key, std::uintmax_t defaultValue) -> std::uintmax_t {
  if (auto const value = readEnv(key); value.has_value()) {
    try {
      return static_cast<std::uintmax_t>(std::stoull(value.value()));
    } catch (...) { }
  }
  return defaultValue;
}

auto hasArg(int argc, char* argv[], std::string_view needle) -> bool {
  for (auto index = 1; index < argc; ++index) {
    if (std::string_view{argv[index]} == needle) { return true; }
  }
  return false;
}

auto readTextFile(fs::path const& path) -> std::optional<std::string> {
  auto input = std::ifstream{path, std::ios::binary};
  if (!input.is_open()) { return std::nullopt; }
  return std::string{std::istreambuf_iterator<char>{input}, {}};
}

auto appendInvocationLog(std::string_view toolName, int argc, char* argv[]) -> void {
  auto const logFile = readEnv("ENCRO_FAKE_TOOL_LOG_FILE");
  if (!logFile.has_value()) { return; }

  auto const logPath = fs::path{logFile.value()};
  if (!logPath.parent_path().empty()) { fs::create_directories(logPath.parent_path()); }

  auto out = std::ofstream{logPath, std::ios::app};
  if (!out.is_open()) { return; }

  out << toolName;
  for (auto index = 1; index < argc; ++index) { out << '\t' << argv[index]; }
  out << '\n';
}

auto writeSizedFile(fs::path const& path, std::uintmax_t sizeInBytes) -> void {
  fs::create_directories(path.parent_path());
  auto out = std::ofstream{path, std::ios::binary};
  if (!out.is_open()) { return; }

  if (sizeInBytes == 0) {
    out.flush();
    return;
  }

  out.seekp(static_cast<std::streamoff>(sizeInBytes - 1));
  out.put('\0');
  out.flush();
}

auto parseDoubleArg(int argc, char* argv[], std::string_view name)
  -> std::optional<double> {
  for (auto index = 1; index + 1 < argc; ++index) {
    if (std::string_view{argv[index]} == name) {
      try {
        return std::stod(argv[index + 1]);
      } catch (...) { return std::nullopt; }
    }
  }
  return std::nullopt;
}

auto parseFfmpegInvocation(int argc, char* argv[]) -> FfmpegInvocation {
  auto invocation = FfmpegInvocation{};

  for (auto index = 1; index < argc; ++index) {
    auto const arg = std::string_view{argv[index]};

    if (
      arg == "-i"
      || arg == "-vf"
      || arg == "-c:v"
      || arg == "-q:v"
      || arg == "-crf"
      || arg == "-loop"
      || arg == "-loglevel"
      || arg == "-progress"
    ) {
      if (index + 1 >= argc) { break; }
      auto const value = fs::path{argv[++index]};
      if (arg == "-progress") { invocation.progressFile = value; }
      continue;
    }

    if (arg == "-hide_banner" || arg == "-nostats" || arg == "-y") { continue; }

    if (!arg.empty() && arg.front() != '-') {
      invocation.outputFile = fs::path{argv[index]};
    }
  }

  invocation.seekSeconds = parseDoubleArg(argc, argv, "-ss");
  invocation.durationSeconds = parseDoubleArg(argc, argv, "-t");

  return invocation;
}

auto emitVersion(std::string_view toolName) -> int {
  std::cout << toolName << " version n5.1-fake\n";
  return 0;
}

auto runFakeFfprobe(int argc, char* argv[]) -> int {
  appendInvocationLog("ffprobe", argc, argv);
  if (hasArg(argc, argv, "-version")) { return emitVersion("ffprobe"); }

  if (auto const stderrText = readEnv("ENCRO_FAKE_FFPROBE_STDERR"); stderrText) {
    std::cerr << stderrText.value();
  }

  auto const exitCode = readEnvInt("ENCRO_FAKE_FFPROBE_EXIT_CODE", 0);
  if (exitCode != 0) { return exitCode; }

  if (auto const jsonFile = readEnv("ENCRO_FAKE_FFPROBE_JSON_FILE"); jsonFile) {
    auto const content = readTextFile(jsonFile.value());
    if (!content.has_value()) {
      std::cerr << "missing fake ffprobe json file\n";
      return 2;
    }

    std::cout << content.value();
    return 0;
  }

  std::cout
    << R"({"format":{"duration":"2.0"},"streams":[{"codec_type":"video","codec_name":"h264","nb_frames":"10","avg_frame_rate":"5/1"}]})";
  return 0;
}

auto runFakeFfmpeg(int argc, char* argv[]) -> int {
  appendInvocationLog("ffmpeg", argc, argv);
  if (hasArg(argc, argv, "-version")) { return emitVersion("ffmpeg"); }

  auto const invocation = parseFfmpegInvocation(argc, argv);

  if (auto const stderrText = readEnv("ENCRO_FAKE_FFMPEG_STDERR"); stderrText) {
    std::cerr << stderrText.value();
    if (stderrText->empty() || stderrText->back() != '\n') { std::cerr << '\n'; }
  }

  if (auto const failMatch = readEnv("ENCRO_FAKE_FFMPEG_FAIL_MATCH"); failMatch) {
    if (
      invocation.outputFile.has_value()
      && invocation.outputFile->string().find(failMatch.value()) != std::string::npos
    ) {
      return readEnvInt("ENCRO_FAKE_FFMPEG_EXIT_CODE", 1);
    }
  }

  auto const delayMs = readEnvInt("ENCRO_FAKE_FFMPEG_DELAY_MS", 0);
  if (delayMs > 0) { std::this_thread::sleep_for(std::chrono::milliseconds(delayMs)); }

  if (invocation.progressFile.has_value()) {
    auto const frameCount = readEnvInt("ENCRO_FAKE_FFMPEG_PROGRESS_FRAMES", 10);
    auto out = std::ofstream{invocation.progressFile.value()};
    if (out.is_open()) {
      out << "frame=" << frameCount << "\n";
      if (invocation.seekSeconds.has_value() && invocation.durationSeconds.has_value()) {
        auto const endUs = static_cast<std::uint64_t>(
          (invocation.seekSeconds.value() + invocation.durationSeconds.value()) * 1e6
        );
        out << "out_time_us=" << endUs << "\n";
      }
      out << "progress=end\n";
    }
  }

  auto const exitCode = readEnvInt("ENCRO_FAKE_FFMPEG_EXIT_CODE", 0);
  if (exitCode != 0) { return exitCode; }

  if (!invocation.outputFile.has_value()) {
    std::cerr << "missing output file\n";
    return 2;
  }

  writeSizedFile(
    invocation.outputFile.value(),
    readEnvSize("ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", 1024)
  );
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  auto const exeName = fs::path{argv[0]}.filename().string();
  if (exeName == "ffprobe" || exeName == "ffprobe.exe") {
    return runFakeFfprobe(argc, argv);
  }

  return runFakeFfmpeg(argc, argv);
}
