#include "infra/stop_signal.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <atomic>

#if defined(_WIN32)
  #include <chrono>
  #include <thread>
#endif

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <csignal>
#endif

DEFINE_LOGGER(logtags::INFRA_SIGNAL);

namespace {

auto gInstalled = std::atomic<bool>{false};
auto gStopRequested = std::atomic<bool>{false};

#if defined(_WIN32)
constexpr auto kCanceledExitCode = 130u;
constexpr auto kForceExitGracePeriod = std::chrono::seconds{3};

auto gForceExitWatchdogStarted = std::atomic<bool>{false};
auto gForceExitDeadlineMs = std::atomic<unsigned long long>{0};

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
        ::ExitProcess(kCanceledExitCode);
      }

      std::this_thread::sleep_for(50ms);
    }
  }).detach();
}

BOOL WINAPI handleConsoleCtrl(DWORD ctrlType) {
  switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT: {
      gStopRequested.store(true, std::memory_order_release);

      auto const deadline =
        nowMs()
        + static_cast<unsigned long long>(
          std::chrono::duration_cast<std::chrono::milliseconds>(kForceExitGracePeriod)
            .count()
        );
      auto expectedDeadline = 0ull;
      if (  //
        !gForceExitDeadlineMs
           .compare_exchange_strong(expectedDeadline, deadline, std::memory_order_acq_rel)
      ) {
        ::ExitProcess(kCanceledExitCode);
      }

      return TRUE;
    }
    default: return FALSE;
  }
}
#else
void handleSignal(int) {
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
}

void reset() {
  gStopRequested.store(false, std::memory_order_release);
#if defined(_WIN32)
  gForceExitDeadlineMs.store(0, std::memory_order_release);
#endif
}

auto isStopRequested() -> bool {
  return gStopRequested.load(std::memory_order_acquire);
}

}  // namespace stopsignal
