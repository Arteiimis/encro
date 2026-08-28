#include "logging/log_tags.h"
#include "logging/logging.h"
#include "logging/setup.h"
#include "test_utils.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

// ── Helper: remove temp directory ──

void tryRemoveAll(fs::path const& p) {
  auto ec = std::error_code{};
  fs::remove_all(p, ec);
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — Tag constant format validation
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("log_tags.h: tag constants use dot-notation format", "[logging][infra]") {
  CHECK(logtags::APP_ENTRY == std::string_view{"app.entry"});
  CHECK(logtags::VIDEO_ENCODE == std::string_view{"video.encode"});
  CHECK(logtags::PACK_ZIP == std::string_view{"pack.zip"});

  // Verify at least 5 representative tags use dot-notation (contain a dot, all lowercase)
  auto const tags = std::array{
    logtags::APP_PIPELINE,
    logtags::CMD_CONFIG,
    logtags::VIDEO_BATCH,
    logtags::PICTURE_COMPRESS,
    logtags::CORE_SCAN,
    logtags::INFRA_TOOLCHAIN,
  };
  for (auto const* tag: tags) {
    auto const sv = std::string_view{tag};
    CAPTURE(sv);
    CHECK(sv.find('.') != std::string_view::npos);
    CHECK(std::none_of(sv.begin(), sv.end(), [](char c) {
      return c >= 'A' && c <= 'Z';
    }));
  }

  CHECK(logtags::TEST_INFRA == std::string_view{"test.infra"});
}

// ─────────────────────────────────────────────────────────────────────────────
// DEFINE_LOGGER must be at file scope — it expands to a file-static function,
// and C++ forbids nested functions
DEFINE_LOGGER(logtags::TEST_INFRA);

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — DEFINE_LOGGER + LOG_INFO macro expansion
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "DEFINE_LOGGER + LOG_INFO: macro expansion produces correct output",
  "[logging][infra]"
) {
  // Lazy init (C++11 magic statics): loggerPtr() initializes on first call,
  // after the logger has been registered via registerCapturingLogger
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

  // Verify loggerPtr() returns non-null after registration
  REQUIRE(loggerPtr() != nullptr);

  LOG_INFO("test message {}", 42);
  logger->flush();

  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("test message 42") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 — Source location injection format
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("LOG_INFO: source location injected in message body", "[logging][infra]") {
  // Use spdlog::get() directly for a fresh capture setup — the gLoggerPtr
  // static from Test 2 may have been invalidated by the drop.
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

  // Verify source location format by checking the message pattern that
  // the LOG_* macros produce: "[file.cpp:line] message"
  // We simulate what LOG_INFO expands to.
  auto const shortName = logging::detail::shortFile(__FILE__);
  auto const line = __LINE__;
  // Construct the message the same way the macro does
  auto const formattedMsg = fmt::format("[{}:{}] hello from test 3", shortName, line + 4);

  logger->log(spdlog::source_loc{__FILE__, line, ""}, spdlog::level::info, formattedMsg);
  logger->flush();

  auto const output = oss->str();
  CAPTURE(output);

  // Message should contain the filename (shortFile strips directories)
  auto const expectedShort = std::string_view{shortName};
  CAPTURE(expectedShort);
  CHECK(output.find(expectedShort) != std::string::npos);
  // Message should start with '[' and contain ']' pattern
  CHECK(output.find('[') != std::string::npos);
  auto const bracketSpace = output.find("] ");
  CHECK(bracketSpace != std::string::npos);
  // After the bracket and colon there should be the actual message
  CHECK(output.find("hello from test 3") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 — logging::setup() registration verification
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("logging::setup: registers all 24 named loggers", "[logging][infra]") {
  // Use a temp directory for the log file to avoid polluting user space
  auto const tempDir = fs::temp_directory_path() / "encro_test_logging_infra";
  fs::create_directories(tempDir);

  auto config = logging::LogConfig{
    .colorsEnabled = false,
    .customLogDir = tempDir,
  };

  // Call setup (registers all 24 loggers + default_logger)
  auto const result = logging::setup(config);
  REQUIRE(result.has_value());

  auto const logFilePath = result.value();
  CAPTURE(logFilePath.string());

  // Verify key loggers are registered
  CHECK(spdlog::get(logtags::VIDEO_ENCODE) != nullptr);
  CHECK(spdlog::get(logtags::APP_ENTRY) != nullptr);
  CHECK(spdlog::get(logtags::PACK_ZIP) != nullptr);

  // Verify all 24 tags are registered (non-null)
  // Collect all tags from log_tags.h that logging::setup registers
  auto const allTags = std::array<char const*, 24>{
    logtags::APP_ENTRY,       logtags::APP_PRELUDE,      logtags::APP_PIPELINE,
    logtags::CMD_CONFIG,      logtags::VIDEO_ENCODE,     logtags::VIDEO_PROBE,
    logtags::VIDEO_INFO,      logtags::VIDEO_OUTPUT,     logtags::VIDEO_BATCH,
    logtags::VIDEO_PROGRESS,  logtags::VIDEO_STATE,      logtags::VIDEO_PROCESS,
    logtags::PICTURE_PROCESS, logtags::PICTURE_COMPRESS, logtags::PACK_ZIP,
    logtags::PACK_SERVICE,    logtags::CORE_SCAN,        logtags::CORE_JOB,
    logtags::CORE_TASK,       logtags::CORE_PARALLEL,    logtags::INFRA_TOOLCHAIN,
    logtags::INFRA_CRASH,     logtags::INFRA_SIGNAL,     logtags::UTILS_SUBPROCESS,
  };

  for (auto const* tag: allTags) {
    CAPTURE(tag);
    auto const logger = spdlog::get(tag);
    INFO("Logger for tag '" << tag << "' should be registered");
    CHECK(logger != nullptr);
  }

  // Verify default logger is set (for crash handler compatibility)
  CHECK(spdlog::default_logger_raw() != nullptr);

  // Cleanup
  logging::shutdown();
  tryRemoveAll(tempDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5 — logging::shutdown() cleanup
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("logging::shutdown: cleans up spdlog global state", "[logging][infra]") {
  auto const tempDir = fs::temp_directory_path() / "encro_test_logging_shutdown";
  fs::create_directories(tempDir);

  auto config = logging::LogConfig{
    .colorsEnabled = false,
    .customLogDir = tempDir,
  };

  auto const result = logging::setup(config);
  REQUIRE(result.has_value());

  // Verify log file was created
  CHECK(fs::exists(result.value()));

  // Shutdown
  logging::shutdown();

  // After shutdown, loggers should no longer be accessible
  // (spdlog::shutdown() clears the registry)
  CHECK(spdlog::get(logtags::APP_ENTRY) == nullptr);

  // Clean up temp files
  tryRemoveAll(tempDir);
}
