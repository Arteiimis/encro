#include "logging/setup.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

auto readFileContent(fs::path const& filePath) -> std::string {
  auto ifs = std::ifstream{filePath};
  REQUIRE(ifs.is_open());
  return std::string{std::istreambuf_iterator<char>{ifs}, {}};
}

}  // namespace

TEST_CASE(
  "crash message appears in per-run log file via direct append",
  "[logging][crash_integration]"
) {
  TempDir temp;

  auto config = logging::LogConfig{
    .verboseEnabled = true,
    .verboseEchoEnabled = false,
    .colorsEnabled = false,
    .customLogDir = temp.path,
  };

  // Setup logging and capture the log file path
  auto const setupResult = logging::setup(config);
  REQUIRE(setupResult.has_value());

  // currentLogFilePath() must return the same path setup() created
  auto const logPath = logging::currentLogFilePath();
  REQUIRE(logPath.has_value());
  CHECK(setupResult.value() == logPath.value());

  // Simulate crash handler behavior: append directly via std::ofstream (bypassing spdlog)
  auto const testMessage = std::string{"[CRASH TEST] direct file append integration"};
  {
    auto ofs = std::ofstream(logPath.value(), std::ios::app);
    REQUIRE(ofs.is_open());
    ofs << testMessage << "\n";
    ofs.close();
  }

  // Verify the message was written to the log file
  auto const content = readFileContent(logPath.value());
  CHECK(content.find(testMessage) != std::string::npos);

  logging::shutdown();
}

TEST_CASE(
  "currentLogFilePath returns valid path after setup",
  "[logging][crash_integration]"
) {
  TempDir temp;

  auto config = logging::LogConfig{
    .verboseEnabled = true,
    .verboseEchoEnabled = false,
    .colorsEnabled = false,
    .customLogDir = temp.path,
  };

  auto const setupResult = logging::setup(config);
  REQUIRE(setupResult.has_value());

  auto const logPath = logging::currentLogFilePath();
  REQUIRE(logPath.has_value());

  // Both must return the same file path
  CHECK(setupResult.value() == logPath.value());

  // The file must exist on disk
  CHECK(fs::exists(logPath.value()));

  logging::shutdown();
}

TEST_CASE(
  "currentLogFilePath returns nullopt after shutdown",
  "[logging][crash_integration]"
) {
  TempDir temp;

  auto config = logging::LogConfig{
    .verboseEnabled = true,
    .verboseEchoEnabled = false,
    .colorsEnabled = false,
    .customLogDir = temp.path,
  };

  auto const setupResult = logging::setup(config);
  REQUIRE(setupResult.has_value());

  // Before shutdown, path must be available
  CHECK(logging::currentLogFilePath().has_value());

  logging::shutdown();

  // After shutdown, path must be cleared (crash handler detects this and falls back)
  CHECK(logging::currentLogFilePath() == std::nullopt);
}
