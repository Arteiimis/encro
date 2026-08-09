#include "infra/stop_signal.h"

#include "infra/crash_runtime.h"
#include "logging/log_tags.h"
#include "logging/logging.h"

#include <atomic>
#include <chrono>

#if defined(_WIN32)
  #include <thread>
  #include <windows.h>
#endif

#if defined(_WIN32)
#else
  #include <csignal>
#endif

DEFINE_LOGGER(logtags::INFRA_SIGNAL);

namespace {

auto gInstalled = std::atomic<bool>{false};
auto gStopRequested = std::atomic<bool>{false};
#if !defined(_WIN32)
auto gStopLogged = std::atomic<bool>{false};
#endif

#if defined(_WIN32)
constexpr auto kCanceledExitCode = 130u;
constexpr auto kDefaultForceExitGracePeriod = std::chrono::seconds{3};

auto gForceExitWatchdogStarted = std::atomic<bool>{false};
auto gForceExitDeadlineMs = std::atomic<unsigned long long>{0};
auto gForceExitGracePeriod = std::chrono::milliseconds{kDefaultForceExitGracePeriod};
stopsignal::ForceExitFn gForceExitFn =
  reinterpret_cast<stopsignal::ForceExitFn>(::ExitProcess);

auto nowMs() -> unsigned long long {
  using namespace std::chrono;
  return static_cast<unsigned long long>(
    duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count()
  );
}

void ensureForceExitWatchdogStarted() {
  auto expected = false;
  if (!gForceExitWatchdogStarted.compare_exchange_strong(expected, true)) { return; }

  std::thread([] {
    using namespace std::chrono_literals;

    while (true) {
      auto const deadline = gForceExitDeadlineMs.load(std::memory_order_acquire);
      if (
        deadline != 0
        && gStopRequested.load(std::memory_order_acquire)
        && nowMs() >= deadline
      ) {
        // Direct write bypasses the async queue — the process is about to die
        // without draining it.
        crash::writeDirectLogLine(
          "force exit: process did not stop within the grace period after a stop request"
        );
        gForceExitFn(kCanceledExitCode);
      }

      std::this_thread::sleep_for(50ms);
    }
  }).detach();
}

void armForceExitDeadline() {
  auto const deadline =
    nowMs()
    + static_cast<unsigned long long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(gForceExitGracePeriod).count()
    );
  auto expectedDeadline = 0ull;
  if (  //
    !gForceExitDeadlineMs
       .compare_exchange_strong(expectedDeadline, deadline, std::memory_order_acq_rel)
  ) {
    // Second stop request while a force-exit deadline is already armed: exit
    // now — but leave a direct record first (the queue will not be drained).
    crash::writeDirectLogLine(
      "force exit: second stop request received before the grace period elapsed"
    );
    ::ExitProcess(kCanceledExitCode);
  }
}

BOOL WINAPI handleConsoleCtrl(DWORD ctrlType) {
  switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT: {
      gStopRequested.store(true, std::memory_order_release);
      LOG_INFO("Stop signal received (type {}); cancellation requested", ctrlType);
      armForceExitDeadline();
      return TRUE;
    }
    default: return FALSE;
  }
}
#else
void handleSignal(int) {
  // Signal handlers must stay async-signal-safe: only set the flag here;
  // the log record is emitted once from isStopRequested() on the polling side.
  gStopRequested.store(true, std::memory_order_release);
}
#endif

}  // namespace

namespace stopsignal {

void installHandler() {
  auto expected = false;
  if (!gInstalled.compare_exchange_strong(expected, true)) { return; }

#if defined(_WIN32)
  ensureForceExitWatchdogStarted();
  SetConsoleCtrlHandler(handleConsoleCtrl, TRUE);
#else
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);
#endif
}

void requestStop() {
  gStopRequested.store(true, std::memory_order_release);
  LOG_INFO("Stop requested; cancellation in progress");
#if defined(_WIN32)
  // Arm the force-exit watchdog like the Ctrl handler does — but only when a
  // handler is installed (i.e. a real process, not an arbitrary test caller).
  if (gInstalled.load(std::memory_order_acquire)) { armForceExitDeadline(); }
#endif
}

void reset() {
  gStopRequested.store(false, std::memory_order_release);
#if defined(_WIN32)
  gForceExitDeadlineMs.store(0, std::memory_order_release);
#endif
}

auto isStopRequested() -> bool {
  auto const requested = gStopRequested.load(std::memory_order_acquire);
#if !defined(_WIN32)
  // Log the cancellation event once on the polling side (signal handlers are
  // not safe to log from). Idempotent and one-shot — an acceptable side
  // effect on a query; renaming across 24 call sites would buy nothing.
  if (requested && !gStopLogged.exchange(true, std::memory_order_acq_rel)) {
    LOG_INFO("Stop signal received; cancellation requested");
  }
#endif
  return requested;
}

#if defined(_WIN32)
// Test-only hooks: shorten the grace period and/or replace
// the process-terminating action so the watchdog path is observable without
// actually exiting the test process. Restore defaults with nullptr/3s.
void setForceExitGracePeriodForTest(std::chrono::milliseconds gracePeriod) {
  gForceExitGracePeriod = gracePeriod;
}

void setForceExitHandlerForTest(stopsignal::ForceExitFn handler) {
  gForceExitFn = handler == nullptr
    ? reinterpret_cast<stopsignal::ForceExitFn>(::ExitProcess)
    : handler;
}
#endif

}  // namespace stopsignal
