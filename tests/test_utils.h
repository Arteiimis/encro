#pragma once

#include "infra/env.h"
#include "infra/stop_signal.h"

#include <catch2/catch_test_macros.hpp>
#include <libzippp/libzippp.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
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
// environment regardless of execution order.
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

inline void writeTextFile(fs::path const& filePath, std::string_view content = "x") {
  auto const parentPath = filePath.parent_path();
  if (!parentPath.empty()) { fs::create_directories(parentPath); }

  auto out = std::ofstream{filePath, std::ios::binary};
  REQUIRE(out.is_open());
  out << content;
}

inline void writeFile(fs::path const& filePath, std::string_view content = "x") {
  writeTextFile(filePath, content);
}

inline void touchFile(fs::path const& filePath) {
  writeTextFile(filePath);
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
