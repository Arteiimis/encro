#include "core/stop_signal.h"

#include <atomic>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <csignal>
#endif

namespace {

auto gInstalled = std::atomic<bool>{false};
auto gStopRequested = std::atomic<bool>{false};

#if defined(_WIN32)
BOOL WINAPI handleConsoleCtrl(DWORD ctrlType) {
  switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      gStopRequested.store(true, std::memory_order_release);
      return TRUE;
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
  SetConsoleCtrlHandler(handleConsoleCtrl, TRUE);
#else
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);
#endif
}

void reset() {
  gStopRequested.store(false, std::memory_order_release);
}

auto isStopRequested() -> bool {
  return gStopRequested.load(std::memory_order_acquire);
}

}  // namespace stopsignal
