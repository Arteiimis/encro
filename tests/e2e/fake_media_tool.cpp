#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

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

int readEnvInt(std::string const& key, int defaultValue) {
  if (auto const value = readEnv(key); value.has_value()) {
    try {
      return std::stoi(value.value());
    } catch (...) { }
  }
  return defaultValue;
}

std::uintmax_t readEnvSize(std::string const& key, std::uintmax_t defaultValue) {
  if (auto const value = readEnv(key); value.has_value()) {
    try {
      return static_cast<std::uintmax_t>(std::stoull(value.value()));
    } catch (...) { }
  }
  return defaultValue;
}

#if defined(_WIN32)
  #include <process.h>
#endif

// Stable per-process id for concurrency proofs (Windows has no getpid).
long processId() {
#if defined(_WIN32)
  return static_cast<long>(::_getpid());
#else
  return static_cast<long>(::getpid());
#endif
}

bool hasArg(int argc, char* argv[], std::string_view needle) {
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

void appendInvocationLog(std::string_view toolName, int argc, char* argv[]) {
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

void writeSizedFile(fs::path const& path, std::uintmax_t sizeInBytes) {
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

int emitVersion(std::string_view toolName) {
  std::cout << toolName << " version n5.1-fake\n";
  return 0;
}

int runFakeFfprobe(int argc, char* argv[]) {
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

// Emulate libvmaf/xpsnr for scoring invocations. Two independent switches:
//   ENCRO_FAKE_FFMPEG_WRITE_VMAF=1  -> write the JSON log at log_path=
//   ENCRO_FAKE_FFMPEG_WRITE_XPSNR=1 -> write per-plane dB lines at stats_file=
// Scores come from *_SCORES (comma-separated, one value per plane/frame slot;
// a single value yields two identical two-frame sets). Off by default so
// probing deterministically falls back to the default CQ.
auto splitScores(std::string const& raw) -> std::vector<std::string> {
  auto values = std::vector<std::string>{};
  auto start = std::size_t{0};
  while (start <= raw.size()) {
    auto const comma = raw.find(',', start);
    values.push_back(
      raw.substr(start, comma == std::string::npos ? std::string::npos : comma - start)
    );
    if (comma == std::string::npos) { break; }
    start = comma + 1;
  }
  if (values.size() == 1) { values.push_back(values.front()); }
  return values;
}

// Extracts the escaped stats_file/log_path='...' target from the
// -filter_complex argument and returns the unescaped path.
auto extractFilterFileTarget(int argc, char* argv[], std::string_view marker)
  -> std::optional<std::string> {
  for (auto index = 1; index < argc; ++index) {
    auto const arg = std::string_view{argv[index]};
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
    return path;
  }
  return std::nullopt;
}

void writeFakeVmafLog(int argc, char* argv[]) {
  auto const values =
    splitScores(readEnv("ENCRO_FAKE_FFMPEG_VMAF_SCORES").value_or("96.0"));

  auto const path = extractFilterFileTarget(argc, argv, "log_path=");
  if (!path.has_value()) { return; }
  auto out = std::ofstream{fs::path{path.value()}};
  if (!out.is_open()) {
    std::cerr << "cannot open vmaf log: " << path.value() << '\n';
    return;
  }
  out << "{\"frames\":[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) { out << ','; }
    out << "{\"frameNum\":" << i << ",\"metrics\":{\"vmaf\":" << values[i] << "}}";
  }
  out << "]}\n";
}

void writeFakeXpsnrStats(int argc, char* argv[]) {
  auto const values =
    splitScores(readEnv("ENCRO_FAKE_FFMPEG_XPSNR_SCORES").value_or("41.5"));

  auto const path = extractFilterFileTarget(argc, argv, "stats_file=");
  if (!path.has_value()) { return; }
  auto out = std::ofstream{fs::path{path.value()}};
  if (!out.is_open()) {
    std::cerr << "cannot open xpsnr stats: " << path.value() << '\n';
    return;
  }
  for (std::size_t i = 0; i < values.size(); ++i) {
    // One value reuses all three planes, keeping the parsed frame mean equal
    // to the configured value.
    out
      << "n:    "
      << (i + 1)
      << "  XPSNR y: "
      << values[i]
      << "  XPSNR u: "
      << values[i]
      << "  XPSNR v: "
      << values[i]
      << '\n';
  }
}

// SSIM variant writing modern "All:" lines at stats_file= so tests can walk
// the chain all the way to the terminal fallback.
void writeFakeSsimStats(int argc, char* argv[]) {
  auto const values =
    splitScores(readEnv("ENCRO_FAKE_FFMPEG_SSIM_SCORES").value_or("0.98"));

  auto const path = extractFilterFileTarget(argc, argv, "stats_file=");
  if (!path.has_value()) { return; }
  auto out = std::ofstream{fs::path{path.value()}};
  if (!out.is_open()) {
    std::cerr << "cannot open ssim stats: " << path.value() << '\n';
    return;
  }
  for (std::size_t i = 0; i < values.size(); ++i) {
    out << "n:" << (i + 1) << " All:" << values[i] << " (dB)\n";
  }
}

auto scoringFailureRequested(int argc, char* argv[]) -> bool {
  auto const match = readEnv("ENCRO_FAKE_FFMPEG_SCORING_FAIL_MATCH");
  if (!match.has_value()) { return false; }
  auto const unless = readEnv("ENCRO_FAKE_FFMPEG_SCORING_FAIL_UNLESS");
  auto matched = false;
  auto exempted = false;
  for (auto index = 1; index < argc; ++index) {
    auto const arg = std::string_view{argv[index]};
    if (arg.find(match.value()) != std::string_view::npos) { matched = true; }
    if (unless && arg.find(unless.value()) != std::string_view::npos) { exempted = true; }
  }
  return matched && !exempted;
}

// Scoring invocations (-f null -) write no output file. Optionally emulate
// libvmaf/xpsnr/ssim (see writeFake*); off by default so probing
// deterministically falls back to the default CQ. Returns the exit code.
auto runScoringInvocation(int argc, char* argv[]) -> int {
  if (scoringFailureRequested(argc, argv)) {
    return readEnvInt("ENCRO_FAKE_FFMPEG_EXIT_CODE", 1);
  }
  if (readEnvInt("ENCRO_FAKE_FFMPEG_WRITE_XPSNR", 0) != 0) {
    writeFakeXpsnrStats(argc, argv);
  }
  if (readEnvInt("ENCRO_FAKE_FFMPEG_WRITE_VMAF", 0) != 0) {
    writeFakeVmafLog(argc, argv);
  }
  if (readEnvInt("ENCRO_FAKE_FFMPEG_WRITE_SSIM", 0) != 0) {
    writeFakeSsimStats(argc, argv);
  }
  return 0;
}

// Emulates -progress output so the parser paths are exercised end to end.
void writeFakeProgressFile(FfmpegInvocation const& invocation) {
  auto const padBytes = readEnvInt("ENCRO_FAKE_FFMPEG_PROGRESS_PAD", 0);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): caller checks has_value
  auto const progressPath = invocation.progressFile.value();
  if (auto const parentPath = progressPath.parent_path(); !parentPath.empty()) {
    std::error_code ec;
    fs::create_directories(parentPath, ec);
  }
  auto out = std::ofstream{progressPath};
  if (!out.is_open()) { return; }
  if (padBytes > 0) {
    // Emulates an old/oversized -progress file so the tail-read path is
    // exercised end to end.
    auto filler = std::string(padBytes, 'x');
    for (auto offset = std::size_t{0}; offset < filler.size(); offset += 101) {
      out << filler.substr(offset, 100) << "\n";
    }
  }
  out << "frame=10\n";
  if (
    readEnvInt("ENCRO_FAKE_FFMPEG_PROGRESS_NO_END_TIME", 0) == 0
    && invocation.seekSeconds.has_value()
    && invocation.durationSeconds.has_value()
  ) {
    auto const endUs = static_cast<std::uint64_t>(
      (invocation.seekSeconds.value() + invocation.durationSeconds.value()) * 1e6
    );
    out << "out_time_us=" << endUs << "\n";
  }
  out << "progress=end\n";
}

// Per-call schedules let one fake binary behave differently per invocation
// index (a delayed second call, failures from call N on), keyed by a
// persistent counter file so sequential child processes share the sequence.
int nextScheduledCallIndex() {
  auto const countFilePath = readEnv("ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE");
  if (!countFilePath.has_value()) { return -1; }

  auto const filePath = fs::path{countFilePath.value()};
  auto previous = 0;
  {
    auto in = std::ifstream{filePath};
    if (in.is_open()) {
      int stored = 0;
      if (in >> stored) { previous = stored; }
    }
  }
  auto const index = previous + 1;
  {
    auto out = std::ofstream{filePath, std::ios::trunc};
    if (out.is_open()) { out << index; }
  }
  return index;
}

struct ScheduleSegment {
  int callIndex;
  int delayMs;
  int exitCode;
  bool fromCallOnwards;
};

// Plan grammar: "<call>[-]:<delayMs>:<exitCode>[;...]" - "2:" targets only
// call 2, "2-" targets call 2 and every later call.
auto parseCallPlan(std::string const& plan) -> std::vector<ScheduleSegment> {
  auto segments = std::vector<ScheduleSegment>{};
  auto stream = std::istringstream{plan};
  std::string part;
  while (std::getline(stream, part, ';')) {
    auto parser = std::istringstream{part};
    std::string callToken;
    std::string delayToken;
    std::string exitToken;
    if (
      !std::getline(parser, callToken, ':')
      || !std::getline(parser, delayToken, ':')
      || !static_cast<bool>(parser >> exitToken)
    ) {
      continue;
    }

    auto segment = ScheduleSegment{};
    try {
      size_t consumed = 0;
      segment.callIndex = std::stoi(callToken, &consumed);
      segment.fromCallOnwards = callToken.find('-', consumed) != std::string::npos;
      segment.delayMs = std::stoi(delayToken);
      segment.exitCode = std::stoi(exitToken);
    } catch (...) { continue; }
    segments.push_back(segment);
  }
  return segments;
}

// Optional completion record (ENCRO_FAKE_FFMPEG_INPUT_LOG): every invocation
// that reaches the success path appends its -i input path, letting tests
// identify which sources a cancelled run actually finished.
void recordCompletedInput(int argc, char* argv[]) {
  auto const dest = readEnv("ENCRO_FAKE_FFMPEG_INPUT_LOG");
  if (!dest.has_value()) { return; }

  std::optional<std::string_view> inputPath;
  for (auto index = 1; index + 1 < argc; ++index) {
    if (std::string_view{argv[index]} == "-i") { inputPath = argv[index + 1]; }
  }
  if (!inputPath.has_value()) { return; }

  auto const logPath = fs::path{dest.value()};
  if (!logPath.parent_path().empty()) {
    auto ec = std::error_code{};
    fs::create_directories(logPath.parent_path(), ec);
  }
  auto out = std::ofstream{logPath, std::ios::app};
  if (!out.is_open()) { return; }
  out << inputPath.value() << '\n';
}

int runFakeFfmpeg(int argc, char* argv[]) {
  appendInvocationLog("ffmpeg", argc, argv);
  if (hasArg(argc, argv, "-version")) { return emitVersion("ffmpeg"); }

  auto const invocation = parseFfmpegInvocation(argc, argv);

  // Resolve this invocation's call index and any matching schedule entry.
  auto scheduledDelayMs = -1;
  auto scheduledExitCode = -1;
  auto const callIndex = nextScheduledCallIndex();
  if (callIndex > 0) {
    if (auto const planText = readEnv("ENCRO_FAKE_FFMPEG_CALL_PLAN"); planText) {
      for (auto const& segment: parseCallPlan(planText.value())) {
        if (
          segment.callIndex != callIndex
          && !(segment.fromCallOnwards && callIndex >= segment.callIndex)
        ) {
          continue;
        }
        scheduledDelayMs = segment.delayMs;
        scheduledExitCode = segment.exitCode;
        break;
      }
    }
  }

  auto const effectiveDelayMs = scheduledDelayMs >= 0
    ? scheduledDelayMs
    : readEnvInt("ENCRO_FAKE_FFMPEG_DELAY_MS", 0);

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

  auto const delayMs = effectiveDelayMs;
  if (delayMs > 0) {
    auto const concurrencyDir = readEnv("ENCRO_FAKE_FFMPEG_CONCURRENCY_DIR");
    if (concurrencyDir) {
      // Record [start, end] into a per-process file under the given directory
      // so tests can assert that two delayed invocations actually overlap
      // (parallel scheduling proof). Per-process files avoid cross-process
      // append races on a shared file. steady_clock timestamps are
      // comparable across processes.
      auto const file = fs::path{concurrencyDir.value()} / std::to_string(processId());
      auto const nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch()
      )
                           .count();
      {
        auto out = std::ofstream{file, std::ios::app};
        if (out.is_open()) { out << nowMs << "\n"; }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
      {
        auto out = std::ofstream{file, std::ios::app};
        if (out.is_open()) { out << nowMs + delayMs << "\n"; }
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
  }

  if (invocation.progressFile.has_value()) { writeFakeProgressFile(invocation); }

  auto const exitCode = scheduledExitCode >= 0
    ? scheduledExitCode
    : readEnvInt("ENCRO_FAKE_FFMPEG_EXIT_CODE", 0);
  if (exitCode != 0) {
    // Optional partial-output-before-failure semantics: some orchestration
    // flows leave a .partial file behind that the parent must clean up.
    auto const failBytes = readEnvSize("ENCRO_FAKE_FFMPEG_FAIL_OUTPUT_BYTES", 0);
    if (failBytes > 0 && invocation.outputFile.has_value()) {
      writeSizedFile(invocation.outputFile.value(), failBytes);
    }
    return exitCode;
  }

  if (!invocation.outputFile.has_value()) {
    std::cerr << "missing output file\n";
    return 2;
  }

  // Scoring invocations (-f null -) produce no output file and are handled
  // by the metric impersonators.
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): guarded by has_value above
  if (invocation.outputFile->string() == "-") { return runScoringInvocation(argc, argv); }

  recordCompletedInput(argc, argv);

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
