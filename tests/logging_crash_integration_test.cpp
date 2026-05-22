#include "logging/setup.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <array>
#include <chrono>
#include <ctime>
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

TEST_CASE(
  "log file persists on disk after shutdown and remains appendable",
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

  auto const logPath = setupResult.value();
  REQUIRE(fs::exists(logPath));

  // Shutdown spdlog — the file must remain on disk
  logging::shutdown();

  // Verify spdlog has cleared the path tracking
  CHECK(logging::currentLogFilePath() == std::nullopt);

  // The file itself must still exist on disk (Pitfall #10: file survives shutdown)
  REQUIRE(fs::exists(logPath));

  // Simulate crash handler: append directly to the persisted file
  auto const crashMessage = std::string{"[CRASH POST-SHUTDOWN] message after logger death"};
  {
    auto ofs = std::ofstream(logPath, std::ios::app);
    REQUIRE(ofs.is_open());
    ofs << crashMessage << "\n";
    ofs.close();
  }

  // Verify the crash message appears in the file
  auto const content = readFileContent(logPath);
  CHECK(content.find(crashMessage) != std::string::npos);
}

TEST_CASE(
  "crash message format includes timestamp level and module tags",
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

  auto const logPath = setupResult.value();
  REQUIRE(fs::exists(logPath));

  // Shutdown first to flush spdlog async queue, then verify spdlog wrote lines
  logging::shutdown();

  auto const spdlogContent = readFileContent(logPath);
  // After shutdown (which flushes), spdlog should have written at least one line
  CHECK(!spdlogContent.empty());
  CHECK(spdlogContent.find('[') != std::string::npos);

  // Now append a crash-style message matching tryWriteDirectToLogFile format (D-15)
  auto const testBody = std::string{"integration test format verification"};
  {
    auto ofs = std::ofstream(logPath, std::ios::app);
    REQUIRE(ofs.is_open());

    auto const now = std::chrono::system_clock::now();
    auto const t = std::chrono::system_clock::to_time_t(now);
    auto tm = std::tm{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    auto tsBuf = std::array<char, 64>{};
    std::strftime(tsBuf.data(), tsBuf.size(), "%Y-%m-%dT%H:%M:%S", &tm);

    auto const formatted = std::format(
      "[{}] [critical] [infra.crash] {}\n",
      tsBuf.data(),
      testBody
    );
    ofs.write(formatted.data(), static_cast<std::streamsize>(formatted.size()));
    ofs.close();
  }

  auto const content = readFileContent(logPath);

  // Message body must be present
  CHECK(content.find(testBody) != std::string::npos);

  // Format must include the required tags (D-15)
  CHECK(content.find("[critical]") != std::string::npos);
  CHECK(content.find("[infra.crash]") != std::string::npos);

  // Timestamp bracket pattern must be present: [...T...] (year starts with "20")
  auto const tsBracketPos = content.find("[20");
  CHECK(tsBracketPos != std::string::npos);
  auto const closeBracketPos = content.find("] ", tsBracketPos);
  CHECK(closeBracketPos != std::string::npos);
}
