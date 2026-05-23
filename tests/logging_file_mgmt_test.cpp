#include "logging/setup.h"

#include <catch2/catch_all.hpp>

#include <fmt/format.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ── Helpers ──────────────────────────────────────────────────────────────────

auto makeTestDir(std::string const& name) -> fs::path {
  auto const dir = fs::temp_directory_path() / "encro_test_fmgmt" / name;
  auto ec = std::error_code{};
  fs::create_directories(dir, ec);
  return dir;
}

auto removeDir(fs::path const& dir) -> void {
  auto ec = std::error_code{};
  fs::remove_all(dir, ec);
}

auto createFakeLogFile(fs::path const& dir, std::string const& filename) -> void {
  auto ofs = std::ofstream{dir / filename};
  ofs << "fake log content\n";
}

auto countEncroFiles(fs::path const& dir) -> std::size_t {
  auto const logPattern = std::regex{R"(encro_.*\.(log|ndjson).*)"};
  auto count = std::size_t{0};
  auto ec = std::error_code{};
  for (auto const& entry: fs::directory_iterator{dir, ec}) {
    if (ec) { break; }
    if (
      entry.is_regular_file()
      && std::regex_match(entry.path().filename().string(), logPattern)
    ) {
      ++count;
    }
  }
  return count;
}

auto sortedEncroFiles(fs::path const& dir) -> std::vector<fs::path> {
  auto files = std::vector<fs::path>{};
  auto const logPattern = std::regex{R"(encro_.*\.log.*)"};
  auto ec = std::error_code{};
  for (auto const& entry: fs::directory_iterator{dir, ec}) {
    if (ec) { break; }
    if (
      entry.is_regular_file()
      && std::regex_match(entry.path().filename().string(), logPattern)
    ) {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

auto isTimestampedName(fs::path const& p) -> bool {
  // Matches encro_YYYYMMDD_HHMMSS.log or encro_YYYYMMDD_HHMMSS_PID.log
  auto const pattern = std::regex{R"(encro_\d{8}_\d{6}(_\d+)?\.log)"};
  return std::regex_match(p.filename().string(), pattern);
}

// ── Constants ────────────────────────────────────────────────────────────────

auto const kTimestampedPattern = std::regex{R"(encro_\d{8}_\d{6}\.log)"};
auto const kTimestampedPidPattern = std::regex{R"(encro_\d{8}_\d{6}_\d+\.log)"};

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Test 1 — setup creates timestamped log file (FILE-01, D-01)
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("setup creates timestamped log file", "[logging][file_mgmt]") {
  auto const testDir = makeTestDir("test1_timestamped_naming");

  auto const config = logging::LogConfig{
    .verboseEnabled = true,
    .verboseEchoEnabled = false,
    .colorsEnabled = false,
    .customLogDir = testDir,
  };

  auto const result = logging::setup(config);
  REQUIRE(result.has_value());

  auto const logFilePath = result.value();
  CAPTURE(logFilePath.string());

  // D-01: filename matches encro_YYYYMMDD_HHMMSS.log pattern
  CHECK(isTimestampedName(logFilePath));

  // FILE-01: the file actually exists on disk
  CHECK(fs::exists(logFilePath));

  // PID collision: create a file with the same timestamp name to force the PID branch.
  // Compute current timestamp and pre-create a file with that name.
  auto const now = std::chrono::system_clock::now();
  auto const nowTimeT = std::chrono::system_clock::to_time_t(now);
  auto tm = std::tm{};
  localtime_s(&tm, &nowTimeT);
  auto tsBuf = std::ostringstream{};
  tsBuf << std::put_time(&tm, "encro_%Y%m%d_%H%M%S.log");
  auto const collisionName = tsBuf.str();

  createFakeLogFile(testDir, collisionName);

  // Shutdown and re-setup to trigger collision detection
  logging::shutdown();

  auto const result2 = logging::setup(config);
  REQUIRE(result2.has_value());

  auto const logFilePath2 = result2.value();
  CAPTURE(logFilePath2.string());

  // After pre-creating a file with the expected timestamp name,
  // the new file should either have a PID suffix (same-second collision)
  // or have a different timestamp (next second). Either way, it must exist.
  CHECK(fs::exists(logFilePath2));
  CHECK(isTimestampedName(logFilePath2));

  // The newly created file should not be the same as the collision file
  CHECK(logFilePath2 != (testDir / collisionName));

  // Cleanup
  logging::shutdown();
  removeDir(testDir);
}

// ══════════════════════════════════════════════════════════════════════════════
// Test 2 — setup returns a valid, usable, timestamped log file path
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("setup returns valid timestamped log file path", "[logging][file_mgmt]") {
  auto const testDir = makeTestDir("test2_valid_path");

  auto const config = logging::LogConfig{
    .verboseEnabled = true,
    .verboseEchoEnabled = false,
    .colorsEnabled = false,
    .customLogDir = testDir,
  };

  auto const result = logging::setup(config);
  REQUIRE(result.has_value());

  auto const logFilePath = result.value();
  CAPTURE(logFilePath.string());

  // The path must be absolute
  CHECK(logFilePath.is_absolute());

  // The filename must match the timestamped naming pattern
  CHECK(isTimestampedName(logFilePath));

  // The file must be physically present on disk
  CHECK(fs::exists(logFilePath));

  // Shutdown (drains async queue, writes pending log messages)
  logging::shutdown();

  // After shutdown, the log file must have non-zero size
  auto ec = std::error_code{};
  auto const fileSize = fs::file_size(logFilePath, ec);
  CHECK(!ec);
  CHECK(fileSize > 0);

  // Cleanup
  removeDir(testDir);
}

// ══════════════════════════════════════════════════════════════════════════════
// Test 3 — cleanup retains at most 10 log files (FILE-02, D-04, D-06)
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("cleanup retains at most 10 log files", "[logging][file_mgmt]") {
  auto const testDir = makeTestDir("test3_retention");

  // Create 15 fake encro_*.log files with different timestamps.
  // Lexicographically unique timestamps ensure deterministic sort order.
  for (auto i = 0; i < 15; ++i) {
    auto const filename = fmt::format("encro_{:08d}_{:06d}.log", 20260523, 100000 + i);
    createFakeLogFile(testDir, filename);
  }

  // Verify 15 files were created
  auto const preCount = countEncroFiles(testDir);
  REQUIRE(preCount == 15);

  auto const config = logging::LogConfig{
    .verboseEnabled = true,
    .verboseEchoEnabled = false,
    .colorsEnabled = false,
    .customLogDir = testDir,
  };

  auto const result = logging::setup(config);
  REQUIRE(result.has_value());

  auto const logFilePath = result.value();
  CAPTURE(logFilePath.string());

  // Shutdown to release file handles
  logging::shutdown();

  // After cleanup, there should be no more than 11 files
  // (10 kept old files + 1 newly created timestamped file).
  auto const postCount = countEncroFiles(testDir);
  CHECK(postCount <= 11);

  // The 10 newest files (by filename) should be the ones with largest timestamps.
  auto const survivingFiles = sortedEncroFiles(testDir);
  CAPTURE(survivingFiles.size());

  // Verify the oldest 5 files (indices 0-4) no longer exist
  for (auto i = 0; i < 5; ++i) {
    auto const expectedDeleted =
      testDir / fmt::format("encro_{:08d}_{:06d}.log", 20260523, 100000 + i);
    CAPTURE(expectedDeleted.string());
    CHECK_FALSE(fs::exists(expectedDeleted));
  }

  // Verify the newest 10 old files still exist
  for (auto i = 5; i < 15; ++i) {
    auto const expectedSurviving =
      testDir / fmt::format("encro_{:08d}_{:06d}.log", 20260523, 100000 + i);
    CAPTURE(expectedSurviving.string());
    CHECK(fs::exists(expectedSurviving));
  }

  // The file created by setup() must also exist
  CHECK(fs::exists(logFilePath));

  // Cleanup
  removeDir(testDir);
}

// ══════════════════════════════════════════════════════════════════════════════
// Test 4 — cleanup matches encro_*.log.* rotation files too (D-05, D-18)
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("cleanup matches rotation files", "[logging][file_mgmt]") {
  auto const testDir = makeTestDir("test4_rotation");

  // Create 8 regular log files and 7 rotation files (.log.1, .log.2, .log.3)
  for (auto i = 0; i < 8; ++i) {
    createFakeLogFile(
      testDir,
      fmt::format("encro_{:08d}_{:06d}.log", 20260523, 100000 + i)
    );
  }
  // Rotation suffix files for the first few log files
  for (auto i = 0; i < 3; ++i) {
    createFakeLogFile(
      testDir,
      fmt::format("encro_{:08d}_{:06d}.log.1", 20260523, 100000 + i)
    );
    createFakeLogFile(
      testDir,
      fmt::format("encro_{:08d}_{:06d}.log.2", 20260523, 100000 + i + 1)
    );
  }
  // One extra .log.3 file
  createFakeLogFile(testDir, "encro_20260523_100005.log.3");

  // Total encro_* files should be 8 + 6 + 1 = 15
  auto const preCount = countEncroFiles(testDir);
  REQUIRE(preCount == 15);

  auto const config = logging::LogConfig{
    .verboseEnabled = true,
    .verboseEchoEnabled = false,
    .colorsEnabled = false,
    .customLogDir = testDir,
  };

  auto const result = logging::setup(config);
  REQUIRE(result.has_value());

  logging::shutdown();

  // D-05, D-18: Rotation suffix files (encro_*.log.*) must also be counted
  // and cleaned. With 15 total + 1 new file, after cleanup there should be
  // at most 11 files (10 kept + 1 new).
  auto const postCount = countEncroFiles(testDir);
  CAPTURE(postCount);
  CHECK(postCount <= 11);

  // Cleanup
  removeDir(testDir);
}

// ══════════════════════════════════════════════════════════════════════════════
// Test 5 — cleanup does NOT delete the file being created (D-04)
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("cleanup does not delete current log file", "[logging][file_mgmt]") {
  auto const testDir = makeTestDir("test5_no_delete_current");

  // Create 15 old log files that precede the current timestamp
  for (auto i = 0; i < 15; ++i) {
    createFakeLogFile(
      testDir,
      fmt::format("encro_{:08d}_{:06d}.log", 20250101, 100000 + i)
    );
  }

  auto const config = logging::LogConfig{
    .verboseEnabled = true,
    .verboseEchoEnabled = false,
    .colorsEnabled = false,
    .customLogDir = testDir,
  };

  auto const result = logging::setup(config);
  REQUIRE(result.has_value());

  auto const logFilePath = result.value();
  CAPTURE(logFilePath.string());

  // D-01: The returned file path must have a timestamped name
  CHECK(isTimestampedName(logFilePath));

  // The file created by this setup() call must exist on disk after shutdown
  logging::shutdown();

  CHECK(fs::exists(logFilePath));

  // Verify the file has the expected timestamped format
  CHECK(isTimestampedName(logFilePath.filename()));

  // Cleanup
  removeDir(testDir);
}

// ══════════════════════════════════════════════════════════════════════════════
// Test 6 — setup continues when primary log dir is unwritable (D-21, D-22)
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("setup falls back when primary log dir is unwritable", "[logging][file_mgmt]") {
  auto const testDir = makeTestDir("test6_unwritable");

  // Create a FILE at the path where setup() would try to create directories.
  // This causes fs::create_directories() to fail because the parent path
  // component is a file, not a directory.
  auto const blockingFilePath = testDir / "blocked";
  auto blockFile = std::ofstream{blockingFilePath};
  blockFile << "block\n";
  blockFile.close();

  // customLogDir points to a path whose parent component is a file
  auto const blockedLogDir = blockingFilePath / "encro" / "logs";

  auto const config = logging::LogConfig{
    .verboseEnabled = true,
    .verboseEchoEnabled = false,
    .colorsEnabled = false,
    .customLogDir = blockedLogDir,
  };

  // D-21: setup() must NOT throw or crash when primary dir is unwritable
  auto const result = logging::setup(config);

  // D-21: Fallback should succeed — setup() returns a valid path (the temp fallback)
  REQUIRE(result.has_value());

  auto const logFilePath = result.value();
  CAPTURE(logFilePath.string());

  // The fallback path must exist and have a timestamped name
  CHECK(fs::exists(logFilePath));
  CHECK(isTimestampedName(logFilePath));

  // Cleanup
  logging::shutdown();
  removeDir(testDir);
}

// ══════════════════════════════════════════════════════════════════════════════
// Test 7 — setup returns nullopt when verboseEnabled is false
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("setup returns nullopt when verbose is disabled", "[logging][file_mgmt]") {
  auto const config = logging::LogConfig{
    .verboseEnabled = false,
    .verboseEchoEnabled = false,
    .colorsEnabled = false,
  };

  auto const result = logging::setup(config);
  CHECK_FALSE(result.has_value());
  CHECK_FALSE(logging::currentLogFilePath().has_value());

  logging::shutdown();
}

// ══════════════════════════════════════════════════════════════════════════════
// Test 8 — rotating file sink is configured and functional (D-17, D-18)
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("rotating file sink is configured and functional", "[logging][file_mgmt]") {
  auto const testDir = makeTestDir("test8_rotating_sink");

  auto const config = logging::LogConfig{
    .verboseEnabled = true,
    .verboseEchoEnabled = false,
    .colorsEnabled = false,
    .customLogDir = testDir,
  };

  auto const result = logging::setup(config);
  REQUIRE(result.has_value());

  auto const logFilePath = result.value();
  CAPTURE(logFilePath.string());

  // Verify the file sink exists by checking the default logger's sinks
  auto const* logger = spdlog::default_logger_raw();
  REQUIRE(logger != nullptr);

  auto const& sinks = logger->sinks();
  CHECK_FALSE(sinks.empty());

  // D-17: The log file must exist and be writable
  CHECK(fs::exists(logFilePath));

  // Write a test message and verify it reaches the file
  auto const testMsg = "rotating_sink_test_"
    + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
  spdlog::info("{}", testMsg);
  spdlog::default_logger()->flush();

  // Shutdown to flush all pending writes
  logging::shutdown();

  // Read back the file content and verify our message is there
  auto ifs = std::ifstream{logFilePath};
  REQUIRE(ifs.is_open());
  auto content =
    std::string{std::istreambuf_iterator<char>{ifs}, std::istreambuf_iterator<char>{}};
  CAPTURE(content);
  CHECK(content.find(testMsg) != std::string::npos);

  // Cleanup
  removeDir(testDir);
}
