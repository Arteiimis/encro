#include "infra/stop_signal.h"
#include "logging/log_tags.h"
#include "logging/setup.h"  // IWYU pragma: keep -- needed with libstdc++; MSVC pulls it transitively
#include "test_utils.h"

#include <spdlog/logger.h>  // IWYU pragma: keep -- needed with libstdc++; MSVC pulls it transitively
#include <spdlog/spdlog.h>

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <atomic>
#include <chrono>
#include <filesystem>  // IWYU pragma: keep -- needed with libstdc++; MSVC pulls it transitively
#include <memory>  // IWYU pragma: keep -- needed with libstdc++; MSVC pulls it transitively
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
    // One-sided: parks the waiter inside waitForStop before the stop
    // fires; correctness does not depend on the duration.
    std::this_thread::sleep_for(std::chrono::milliseconds{30});  // sleep-ok: signal delay
    stopsignal::requestStop();
  }};

  // Returning true is the assertion (the event was observed); an elapsed
  // responsiveness bound would measure the machine, not the code.
  CHECK(stopsignal::waitForStop(std::chrono::seconds{5}));
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

  // Poll for the watchdog's forced-exit hook instead of a fixed poll loop.
  CHECK(testutils::waitUntil([] { return gExitCalled.load(); }, std::chrono::seconds{5}));

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
