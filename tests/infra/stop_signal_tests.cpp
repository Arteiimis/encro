#include "infra/stop_signal.h"
#include "logging/log_tags.h"
#include "logging/logging.h"
#include "logging/setup.h"
#include "test_utils.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

auto gExitCalled = std::atomic<bool>{false};

}  // namespace

TEST_CASE("requestStop produces an info log record", "[stop-signal][logging]") {
  auto resetGuard = testutils::ScopedStopSignalReset{};
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::INFRA_SIGNAL);

  stopsignal::requestStop();
  logger->flush();

  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("Stop requested") != std::string::npos);
  CHECK(output.find("cancellation in progress") != std::string::npos);

  // Cleanup: remove the test logger so later logging::setup() calls (in other
  // test cases) can re-register the full named-logger set without conflicts.
  spdlog::drop(logtags::INFRA_SIGNAL);
}

TEST_CASE(
  "waitForStop reports signaled after request and cleared after reset",
  "[stop-signal]"
) {
  auto resetGuard = testutils::ScopedStopSignalReset{};

  CHECK_FALSE(stopsignal::isStopRequested());
  CHECK_FALSE(stopsignal::waitForStop(std::chrono::milliseconds{0}));

  stopsignal::requestStop();

  CHECK(stopsignal::isStopRequested());
  // Manual-reset semantics: once signaled it stays signaled across waits.
  CHECK(stopsignal::waitForStop(std::chrono::milliseconds{0}));
  CHECK(stopsignal::waitForStop(std::chrono::milliseconds{10}));

  stopsignal::reset();

  CHECK_FALSE(stopsignal::isStopRequested());
  CHECK_FALSE(stopsignal::waitForStop(std::chrono::milliseconds{0}));
}

TEST_CASE(
  "waitForStop wakes before its timeout when stop is requested",
  "[stop-signal]"
) {
  auto resetGuard = testutils::ScopedStopSignalReset{};

  auto requester = std::jthread{[] {
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    stopsignal::requestStop();
  }};

  auto const start = std::chrono::steady_clock::now();
  CHECK(stopsignal::waitForStop(std::chrono::seconds{5}));
  auto const elapsed = std::chrono::steady_clock::now() - start;

  CHECK(elapsed < std::chrono::seconds{1});
}

TEST_CASE(
  "force-exit watchdog writes a direct log line before terminating",
  "[stop-signal][logging]"
) {
#if defined(_WIN32)
  auto resetGuard = testutils::ScopedStopSignalReset{};
  TempDir temp;

  auto config = logging::LogConfig{
    .colorsEnabled = false,
    .customLogDir = temp.path,
  };
  // Clear any test-registered logger with the same tag before setup()
  // re-registers the full set of named loggers.
  spdlog::drop(logtags::INFRA_SIGNAL);
  auto const setupResult = logging::setup(config);
  REQUIRE(setupResult.has_value());
  auto const logPath = setupResult.value();

  // Start the watchdog (idempotent), then make it observable: short grace
  // period + a no-op exit action instead of ExitProcess.
  stopsignal::installHandler();
  stopsignal::setForceExitGracePeriodForTest(std::chrono::milliseconds{50});
  gExitCalled.store(false);
  stopsignal::setForceExitHandlerForTest([](unsigned int) { gExitCalled.store(true); });

  stopsignal::requestStop();

  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
  while (!gExitCalled.load() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  CHECK(gExitCalled.load());

  logging::shutdown();

  // Clear the stop state before restoring the real force-exit action: the
  // watchdog re-checks the armed deadline every 50 ms, and once the handler
  // is ExitProcess again a stale armed deadline would kill the test process.
  stopsignal::reset();

  // Restore defaults so later tests are unaffected.
  stopsignal::setForceExitGracePeriodForTest(std::chrono::seconds{3});
  stopsignal::setForceExitHandlerForTest(nullptr);

  auto const content = testutils::readTextFile(logPath);
  CHECK(content.find("force exit") != std::string::npos);
#else
  SKIP("force-exit watchdog is Windows-only");
#endif
}
