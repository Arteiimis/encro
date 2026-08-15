#include "infra/stop_signal.h"

#include "infra/crash_runtime.h"
#include "logging/log_tags.h"
#include "logging/logging.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>

#if defined(_WIN32)
  #include <thread>
  #include <windows.h>
#endif

#if defined(_WIN32)
#else
  #include <csignal>
  #include <fcntl.h>
  #include <mutex>
  #include <poll.h>
  #include <unistd.h>
#endif

DEFINE_LOGGER(logtags::INFRA_SIGNAL);

namespace {

auto gInstalled = std::atomic<bool>{false};
auto gStopRequested = std::atomic<bool>{false};
#if defined(_WIN32)
auto gStopEvent = std::atomic<HANDLE>{nullptr};
#else
auto gStopPipeReadFd = std::atomic<int>{-1};
auto gStopPipeWriteFd = std::atomic<int>{-1};
auto gStopPipeOnce = std::once_flag{};
auto gStopLogged = std::atomic<bool>{false};
#endif

void signalStopEvent() {
#if defined(_WIN32)
  if (auto const handle = gStopEvent.load(std::memory_order_acquire); handle != nullptr) {
    ::SetEvent(handle);
  }
#else
  std::call_once(gStopPipeOnce, [] {
    auto fds = std::array<int, 2>{-1, -1};
    if (::pipe(fds.data()) != 0) { return; }
    ::fcntl(fds[0], F_SETFL, O_NONBLOCK);
    ::fcntl(fds[1], F_SETFL, O_NONBLOCK);
    gStopPipeReadFd.store(fds[0], std::memory_order_release);
    gStopPipeWriteFd.store(fds[1], std::memory_order_release);
  });
  // The handler can only write (async-signal-safe); a full pipe already
  // means the event is signaled, so EAGAIN is fine.
  if (auto const fd = gStopPipeWriteFd.load(std::memory_order_acquire); fd != -1) {
    auto const byte = char{1};
    (void)::write(fd, &byte, 1);
  }
#endif
}

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

      // Plain sleep: the watchdog is a backstop with 50 ms granularity and
      // never exits, so a wake-on-stop wait adds a hot-spin risk (the
      // manual-reset event stays signaled) with no benefit.
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
      signalStopEvent();
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
  signalStopEvent();
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
  signalStopEvent();
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
  if (auto const handle = gStopEvent.load(std::memory_order_acquire); handle != nullptr) {
    ::ResetEvent(handle);
  }
#else
  if (auto const fd = gStopPipeReadFd.load(std::memory_order_acquire); fd != -1) {
    auto buffer = std::array<char, 64>{};
    while (::read(fd, buffer.data(), buffer.size()) > 0) { }
  }
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

auto stopEventHandle() -> StopEventHandle {
#if defined(_WIN32)
  auto handle = gStopEvent.load(std::memory_order_acquire);
  if (handle != nullptr) { return handle; }

  auto const created = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (created == nullptr) { return nullptr; }

  auto expected = HANDLE{nullptr};
  if (gStopEvent.compare_exchange_strong(expected, created, std::memory_order_acq_rel)) {
    handle = created;
    // A stop requested before the event existed must not be lost: the
    // creator re-checks the flag and propagates it into the event.
    if (gStopRequested.load(std::memory_order_acquire)) { ::SetEvent(handle); }
  } else {
    ::CloseHandle(created);
    handle = expected;
  }
  return handle;
#else
  std::call_once(gStopPipeOnce, [] {
    auto fds = std::array<int, 2>{-1, -1};
    if (::pipe(fds.data()) != 0) { return; }
    ::fcntl(fds[0], F_SETFL, O_NONBLOCK);
    ::fcntl(fds[1], F_SETFL, O_NONBLOCK);
    gStopPipeReadFd.store(fds[0], std::memory_order_release);
    gStopPipeWriteFd.store(fds[1], std::memory_order_release);
  });
  return gStopPipeReadFd.load(std::memory_order_acquire);
#endif
}

auto waitForStop(std::chrono::milliseconds timeout) -> bool {
  if (isStopRequested()) { return true; }
#if defined(_WIN32)
  auto const handle = stopEventHandle();
  if (handle == nullptr) { return isStopRequested(); }
  auto const timeoutMs =
    static_cast<DWORD>(std::clamp<std::int64_t>(timeout.count(), 0, 0x7FFFFFFF));
  auto const waitResult = ::WaitForSingleObject(handle, timeoutMs);
  return waitResult == WAIT_OBJECT_0 || isStopRequested();
#else
  auto const fd = stopEventHandle();
  if (fd == -1) { return isStopRequested(); }
  auto pollFd = ::pollfd{.fd = fd, .events = POLLIN, .revents = 0};
  auto const timeoutMs =
    static_cast<int>(std::clamp<std::int64_t>(timeout.count(), 0, 0x7FFFFFFF));
  if (::poll(&pollFd, 1, timeoutMs) > 0) {
    // Drain so a stale byte cannot cause a hot wake-up loop.
    auto byte = char{};
    while (::read(fd, &byte, 1) > 0) { }
  }
  return isStopRequested();
#endif
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
