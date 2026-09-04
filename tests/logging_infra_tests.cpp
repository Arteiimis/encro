#include "logging/log_tags.h"
#include "logging/logging.h"
#include "logging/setup.h"
#include "test_utils.h"

#include <spdlog/logger.h>  // IWYU pragma: keep -- needed with libstdc++; MSVC pulls it transitively
#include <spdlog/spdlog.h>

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <cstdlib>  // IWYU pragma: keep -- needed with MSVC STL; Linux libstdc++ pulls it transitively
#include <filesystem>
#include <memory>  // IWYU pragma: keep -- needed with libstdc++; MSVC pulls it transitively
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
// Test 4 — logging::setup() registration verification (spot checks per D6)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("logging::setup: registers the named loggers", "[logging][infra]") {
  // Use a temp directory for the log file to avoid polluting user space
  auto const tempDir = fs::temp_directory_path() / "encro_test_logging_infra";
  fs::create_directories(tempDir);

  auto config = logging::LogConfig{
    .colorsEnabled = false,
    .customLogDir = tempDir,
  };

  // Call setup (registers the named loggers + default_logger)
  auto const result = logging::setup(config);
  REQUIRE(result.has_value());

  auto const logFilePath = result.value();
  CAPTURE(logFilePath.string());

  // Spot-check one logger per tier (full inventory is wiring copied from
  // setup.cpp — a change-detector, not a test)
  CHECK(spdlog::get(logtags::VIDEO_ENCODE) != nullptr);
  CHECK(spdlog::get(logtags::PACK_ZIP) != nullptr);
  CHECK(spdlog::get(logtags::CORE_SCAN) != nullptr);

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
