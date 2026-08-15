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
#if defined(_WIN32)
  // getenv is deprecated by the MSVC CRT; use the safe variant.
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, key.c_str()) != 0 || value == nullptr) {
    return std::nullopt;
  }
  auto result = std::string{value};
  std::free(value);
  return result;
#else
  auto const* value = std::getenv(key.c_str());
  if (value == nullptr) { return std::nullopt; }
  return std::string{value};
#endif
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
  if (!path.parent_path().empty()) { fs::create_directories(path.parent_path()); }
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
      || arg == "-f"
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

    // A lone "-" is the null-muxer output sentinel, not a flag.
    if (arg.size() > 1 && arg.front() == '-') { continue; }
    if (!arg.empty()) { invocation.outputFile = fs::path{argv[index]}; }
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

  // Opt-in input validation: a real ffprobe fails on a missing input; the
  // default off keeps the existing env-var contract unchanged.
  if (readEnvInt("ENCRO_FAKE_FFPROBE_CHECK_INPUT", 0) != 0) {
    auto probed = std::optional<fs::path>{};
    for (auto index = 1; index < argc; ++index) {
      auto const arg = std::string_view{argv[index]};
      if (!arg.empty() && arg.front() != '-') { probed = fs::path{argv[index]}; }
    }
    if (!probed.has_value() || !fs::exists(probed.value())) {
      std::cerr << "probe input not found\n";
      return 2;
    }
  }

  if (auto const jsonFile = readEnv("ENCRO_FAKE_FFPROBE_JSON_FILE"); jsonFile) {
    auto const content = readTextFile(jsonFile.value());
    if (!content.has_value()) {
      std::cerr << "missing fake ffprobe json file\n";
      return 2;
    }

    std::cout << content.value();
    return 0;
  }

  auto duration = readEnv("ENCRO_FAKE_FFPROBE_DURATION_SECS").value_or("2.0");
  auto codecName = readEnv("ENCRO_FAKE_FFPROBE_CODEC_NAME").value_or("h264");
  auto output = std::string{
    R"({"format":{"duration":"@@DURATION@@","size":"1000000"},"streams":[{"codec_type":"video","codec_name":"@@CODEC@@","width":1920,"height":1080,"avg_frame_rate":"30/1","nb_frames":"600","bit_rate":"4000000"}]})"
  };
  auto const pos = output.find("@@DURATION@@");
  if (pos != std::string::npos) { output.replace(pos, 12, duration); }
  auto const codecPos = output.find("@@CODEC@@");
  if (codecPos != std::string::npos) { output.replace(codecPos, 9, codecName); }
  std::cout << output << std::endl;
  return 0;
}

// Emulate libvmaf for scoring invocations: write the JSON log at the path in
// the -filter_complex log_path= option. Off by default (probing then falls
// back to the default CQ); unit tests enable it via
// ENCRO_FAKE_FFMPEG_WRITE_VMAF=1 with scores from ENCRO_FAKE_FFMPEG_VMAF_SCORES
// (comma-separated, one frame each; a single value yields two identical frames).
auto writeFakeVmafLog(int argc, char* argv[]) -> void {
  auto const scores = readEnv("ENCRO_FAKE_FFMPEG_VMAF_SCORES").value_or("96.0");
  auto values = std::vector<std::string>{};
  {
    auto start = std::size_t{0};
    while (start <= scores.size()) {
      auto const comma = scores.find(',', start);
      values.push_back(scores.substr(
        start,
        comma == std::string::npos ? std::string::npos : comma - start
      ));
      if (comma == std::string::npos) { break; }
      start = comma + 1;
    }
  }
  if (values.size() == 1) { values.push_back(values.front()); }

  for (auto index = 1; index < argc; ++index) {
    auto const arg = std::string_view{argv[index]};
    auto const marker = std::string_view{"log_path="};
    auto const markerPos = arg.find(marker);
    if (markerPos == std::string_view::npos) { continue; }
    auto rest = arg.substr(markerPos + marker.size());
    if (rest.empty() || rest.front() != '\'') { continue; }
    rest.remove_prefix(1);
    auto const end = rest.find('\'');
    if (end == std::string_view::npos) { continue; }
    // The path is escaped for the filtergraph ('\:' for the drive colon).
    auto path = std::string{};
    auto const part = rest.substr(0, end);
    for (std::size_t i = 0; i < part.size(); ++i) {
      if (part[i] == '\\' && i + 1 < part.size() && part[i + 1] == ':') {
        path += ':';
        ++i;
      } else {
        path += part[i];
      }
    }
    auto out = std::ofstream{fs::path{path}};
    if (!out.is_open()) {
      std::cerr << "cannot open vmaf log: " << path << '\n';
      return;
    }
    out << "{\"frames\":[";
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (i > 0) { out << ','; }
      out << "{\"frameNum\":" << i << ",\"metrics\":{\"vmaf\":" << values[i] << "}}";
    }
    out << "]}\n";
    return;
  }
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

  // Scoring invocations (-f null -) write no output file. Optionally emulate
  // libvmaf by writing the JSON log (see writeFakeVmafLog); off by default so
  // probing deterministically falls back to the default CQ.
  if (invocation.outputFile->string() == "-") {
    if (readEnvInt("ENCRO_FAKE_FFMPEG_WRITE_VMAF", 0) != 0) {
      writeFakeVmafLog(argc, argv);
    }
    return 0;
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
