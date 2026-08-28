#pragma once

#include "cmd/cmd.h"
#include "infra/env.h"
#include "infra/stop_signal.h"

#include <catch2/catch_test_macros.hpp>
#include <libzippp/libzippp.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#if defined(_WIN32)
  #include <io.h>
#else
  #include <unistd.h>
#endif

namespace fs = std::filesystem;

struct TempDir {
  fs::path path;

  TempDir() {
    path = fs::temp_directory_path();
    path /= std::format(
      "video_encoder_tests_{}",
      std::chrono::steady_clock::now().time_since_epoch().count()
    );
    fs::create_directories(path);
  }

  ~TempDir() {
    // REQUIRE failures unwind through an exception, so a destructor running
    // mid-unwind means the test is failing: keep the evidence and point at it.
    // fprintf/fflush (not std::cerr): the C++ stream state is process-global
    // and other tests may leave it redirected/buffered, losing the hint.
    if (std::uncaught_exceptions() > 0) {
      std::fprintf(stderr, "kept temp dir on failure: %s\n", path.string().c_str());
      std::fflush(stderr);
      return;
    }
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

namespace testutils {

struct ScopedStopSignalReset {
  ScopedStopSignalReset() { stopsignal::reset(); }

  ~ScopedStopSignalReset() { stopsignal::reset(); }
};

// Environment variable scoped to the current test case: restores the previous
// value (or unset state) on destruction so later cases observe a clean
// environment regardless of execution order. Caveat: a pre-case value of ""
// cannot be distinguished from unset on Windows (_putenv_s("N", "") unsets),
// so restoring an originally-empty variable removes it instead.
class ScopedEnvVar {
public:
  ScopedEnvVar(std::string name, std::string value)
    : name_(std::move(name)), hadOriginal_(false) {
    auto const original = processenv::readEnvVar(name_);
    if (original.has_value()) {
      originalValue_ = *original;
      hadOriginal_ = true;
    }
    set(value);
  }

  ScopedEnvVar(ScopedEnvVar const&) = delete;
  auto operator=(ScopedEnvVar const&) -> ScopedEnvVar& = delete;

  ~ScopedEnvVar() {
    if (hadOriginal_) {
      set(originalValue_);
    } else {
#if defined(_WIN32)
      // CRT removal semantics: an empty value stands in for "unset".
      _putenv_s(name_.c_str(), "");
#else
      ::unsetenv(name_.c_str());
#endif
    }
  }

private:
  void set(std::string const& value) const {
#if defined(_WIN32)
    _putenv_s(name_.c_str(), value.c_str());
#else
    ::setenv(name_.c_str(), value.c_str(), 1);
#endif
  }

  std::string name_;
  std::string originalValue_;
  bool hadOriginal_;
};

// Fake ffmpeg/ffprobe = the e2e fake_media_tool binary (FAKE_TOOL_EXE_PATH,
// injected by xmake from target tests' encro_e2e_tool dependency), copied
// under a role name so argv[0] basename selects ffprobe vs ffmpeg. Same call
// works on Windows (.exe suffix added) and POSIX.
#ifndef FAKE_TOOL_EXE_PATH
  #error "FAKE_TOOL_EXE_PATH missing: build the tests target through xmake"
#endif
inline auto copyFakeTool(fs::path const& dir, std::string const& name) -> fs::path {
  auto const fileName = std::string{name}
#if defined(_WIN32)
    + ".exe"
#endif
    ;
  auto const dst = dir / fileName;
  fs::copy_file(fs::path{FAKE_TOOL_EXE_PATH}, dst, fs::copy_options::overwrite_existing);
  return dst;
}

inline auto writeTextFile(fs::path const& filePath, std::string_view content = "x")
  -> fs::path {
  auto const parentPath = filePath.parent_path();
  if (!parentPath.empty()) { fs::create_directories(parentPath); }

  auto out = std::ofstream{filePath, std::ios::binary};
  REQUIRE(out.is_open());
  out << content;
  return filePath;
}

// Creates a file of the given byte size without allocating a big buffer
// (seek-past-end trick: sparse on filesystems that support it). The value of
// the interior bytes is unspecified; tests only depend on the file size.
// Returns the file path so call sites can chain like the old helpers did.
inline auto writeSizedFile(fs::path const& filePath, std::uintmax_t sizeBytes)
  -> fs::path {
  auto const parentPath = filePath.parent_path();
  if (!parentPath.empty()) { fs::create_directories(parentPath); }

  auto out = std::ofstream{filePath, std::ios::binary};
  REQUIRE(out.is_open());
  if (sizeBytes > 0) {
    out.seekp(static_cast<std::streamoff>(sizeBytes - 1));
    out.put('\0');
  }
  return filePath;
}

// Canonical ffprobe metadata fixture: writes the JSON next to the tool copies
// and scopes the ENCRO_FAKE_FFPROBE_JSON_FILE env var for the test case.
inline constexpr auto kFakeProbeJson =
  R"({"format":{"duration":"2.0"},"streams":[{"codec_type":"video","codec_name":"h264","nb_frames":"10","avg_frame_rate":"5/1"}]})";

inline auto copyFakeProbe(fs::path const& dir) -> ScopedEnvVar {
  auto const probeJsonPath = dir / "fake-ffprobe.json";
  writeTextFile(probeJsonPath, kFakeProbeJson);
  return ScopedEnvVar{"ENCRO_FAKE_FFPROBE_JSON_FILE", probeJsonPath.string()};
}

inline auto writeFile(fs::path const& filePath, std::string_view content = "x")
  -> fs::path {
  return writeTextFile(filePath, content);
}

inline auto touchFile(fs::path const& filePath) -> fs::path {
  return writeTextFile(filePath);
}

inline auto stripCollisionSafePrefix(std::string_view entryName) -> std::string_view {
  constexpr auto kFlatEntryPrefix = std::string_view{"1000__"};
  return entryName.starts_with(kFlatEntryPrefix)
    ? entryName.substr(kFlatEntryPrefix.size())
    : entryName;
}

inline bool hasCollisionSafePrefix(
  std::string_view entryName,
  std::string_view dirLabel,
  std::string_view stem
) {
  auto const normalized = stripCollisionSafePrefix(entryName);
  return normalized.starts_with(std::format("{}__", dirLabel))
    && normalized.find(std::format("__{}__", stem)) != std::string_view::npos;
}

inline auto collisionGroupPrefix(std::string_view entryName) -> std::string {
  auto const normalized = stripCollisionSafePrefix(entryName);
  auto const lastSep = normalized.rfind("__");
  if (lastSep == std::string_view::npos) { return std::string{normalized}; }

  auto const stemSep = normalized.rfind("__", lastSep - 1);
  if (stemSep == std::string_view::npos) {
    return std::string{normalized.substr(0, lastSep)};
  }

  return std::string{normalized.substr(0, stemSep)};
}

inline auto listRegularFiles(fs::path const& dirPath) -> std::vector<fs::path> {
  auto files = std::vector<fs::path>{};
  if (!fs::exists(dirPath)) { return files; }

  for (auto const& entry: fs::directory_iterator{dirPath}) {
    if (entry.is_regular_file()) { files.push_back(entry.path()); }
  }

  std::ranges::sort(files);
  return files;
}

inline auto listZipRegularEntryNames(fs::path const& zipPath)
  -> std::vector<std::string> {
  auto zip = libzippp::ZipArchive{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);

  auto entryNames = std::vector<std::string>{};
  auto const entries = zip.getEntries();
  entryNames.reserve(entries.size());
  for (auto const& entry: entries) {
    if (entry.getName().ends_with('/')) { continue; }
    entryNames.emplace_back(entry.getName());
  }

  std::ranges::sort(entryNames);
  zip.close();
  return entryNames;
}

// Redirects stdout to a file until destroyed (used to assert console output).
struct StdoutCapture {
  fs::path file;
  int oldFd = -1;

  explicit StdoutCapture(fs::path const& capturePath): file(capturePath) {
    std::fflush(stdout);
#if defined(_WIN32)
    oldFd = _dup(_fileno(stdout));
    auto* cap = static_cast<std::FILE*>(nullptr);
    fopen_s(&cap, file.string().c_str(), "w");
#else
    oldFd = dup(fileno(stdout));
    auto* cap = std::fopen(file.string().c_str(), "w");
#endif
    REQUIRE(oldFd >= 0);
    if (cap == nullptr) {
#if defined(_WIN32)
      _close(oldFd);
#else
      close(oldFd);
#endif
      oldFd = -1;
    }
    REQUIRE(cap != nullptr);
#if defined(_WIN32)
    _dup2(_fileno(cap), _fileno(stdout));
#else
    dup2(fileno(cap), fileno(stdout));
#endif
    std::fclose(cap);
  }

  StdoutCapture(StdoutCapture const&) = delete;
  auto operator=(StdoutCapture const&) -> StdoutCapture& = delete;

  ~StdoutCapture() {
    std::fflush(stdout);
    if (oldFd >= 0) {
#if defined(_WIN32)
      _dup2(oldFd, _fileno(stdout));
      _close(oldFd);
#else
      dup2(oldFd, fileno(stdout));
      close(oldFd);
#endif
    }
  }
};

inline auto readTextFile(fs::path const& filePath) -> std::string {
  auto ifs = std::ifstream{filePath};
  REQUIRE(ifs.is_open());
  return {std::istreambuf_iterator<char>{ifs}, std::istreambuf_iterator<char>{}};
}

// Redirects stderr to a file until destroyed (used to assert the failure
// path hint printed by TempDir; mirror of StdoutCapture).
struct StderrCapture {
  fs::path file_;
  int oldFd_ = -1;

  explicit StderrCapture(fs::path const& capturePath): file_(capturePath) {
    std::fflush(stderr);
#if defined(_WIN32)
    oldFd_ = _dup(_fileno(stderr));
    auto* cap = static_cast<std::FILE*>(nullptr);
    fopen_s(&cap, file_.string().c_str(), "w");
#else
    oldFd_ = dup(fileno(stderr));
    auto* cap = std::fopen(file_.string().c_str(), "w");
#endif
    REQUIRE(oldFd_ >= 0);
    if (cap == nullptr) {
#if defined(_WIN32)
      _close(oldFd_);
#else
      close(oldFd_);
#endif
      oldFd_ = -1;
    }
    REQUIRE(cap != nullptr);
#if defined(_WIN32)
    _dup2(_fileno(cap), _fileno(stderr));
#else
    dup2(fileno(cap), fileno(stderr));
#endif
    std::fclose(cap);
  }

  StderrCapture(StderrCapture const&) = delete;
  auto operator=(StderrCapture const&) -> StderrCapture& = delete;

  ~StderrCapture() {
    std::fflush(stderr);
    if (oldFd_ >= 0) {
#if defined(_WIN32)
      _dup2(oldFd_, _fileno(stderr));
      _close(oldFd_);
#else
      dup2(oldFd_, fileno(stderr));
      close(oldFd_);
#endif
    }
  }
};

}  // namespace testutils

// ── Capturing logger helpers ─────────────────────────────────────────────

namespace testutils {

// Keeps captured ostringstreams alive for the whole process so logger sinks
// can safely hold references into them.
inline auto keepCaptureStreamAlive(std::unique_ptr<std::ostringstream> oss)
  -> std::ostringstream* {
  static auto sstreams = std::vector<std::unique_ptr<std::ostringstream>>{};
  auto* ossPtr = oss.get();
  sstreams.push_back(std::move(oss));
  return ossPtr;
}

// Drops any previous registration under the same name, then registers.
inline void reregisterLogger(std::shared_ptr<spdlog::logger> const& logger) {
  if (spdlog::get(logger->name()) != nullptr) { spdlog::drop(logger->name()); }
  spdlog::register_logger(logger);
}

// Registers a capturing test logger ("%v" pattern, trace level, always
// flush). Returns the logger and a stream that receives everything logged.
inline auto registerCapturingLogger(char const* name)
  -> std::pair<std::shared_ptr<spdlog::logger>, std::ostringstream*> {
  auto oss = std::make_unique<std::ostringstream>();
  auto* ossPtr = oss.get();
  auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(*ossPtr);
  auto logger = std::make_shared<spdlog::logger>(name, sink);
  logger->set_pattern("%v");
  logger->set_level(spdlog::level::trace);
  logger->flush_on(spdlog::level::trace);
  reregisterLogger(logger);
  keepCaptureStreamAlive(std::move(oss));
  return {logger, ossPtr};
}

// Counts (possibly overlapping) occurrences of "needle" in "text".
inline auto countOccurrences(std::string_view text, std::string_view needle)
  -> std::size_t {
  auto count = std::size_t{0};
  auto pos = text.find(needle);
  while (pos != std::string_view::npos) {
    ++count;
    pos = text.find(needle, pos + 1);
  }
  return count;
}

// Finds the first line in "text" containing "needle" (help-text assertions).
inline auto findHelpLine(std::string_view text, std::string_view needle)
  -> std::optional<std::string> {
  auto start = std::size_t{0};

  while (start <= text.size()) {
    auto const end = text.find('\n', start);
    auto const line = end == std::string_view::npos ? text.substr(start)
                                                    : text.substr(start, end - start);

    if (line.find(needle) != std::string_view::npos) { return std::string{line}; }
    if (end == std::string_view::npos) { break; }
    start = end + 1;
  }

  return std::nullopt;
}

// Parses the given argument vector through the real commandLineInit entry
// point (CLI11 owns argv[] storage, so the strings are kept alive here).
inline auto parseArgs(std::vector<std::string> const& args) -> CmdParseResult {
  static thread_local std::vector<std::string> storage;
  storage = args;
  auto argv = std::vector<char*>{};
  argv.reserve(storage.size());
  for (auto& arg: storage) { argv.push_back(arg.data()); }
  argv.push_back(nullptr);

  return commandLineInit(static_cast<int>(argv.size() - 1), argv.data(), "");
}

}  // namespace testutils
