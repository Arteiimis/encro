#pragma once

#include <chrono>

namespace stopsignal {

constexpr auto kCanceledExitCode = 130;

// Native waitable object signaled on stop request: Windows event handle
// (HANDLE), POSIX self-pipe read end. Created lazily on first use. Manual-
// reset semantics: once signaled it stays signaled until reset().
#if defined(_WIN32)
using StopEventHandle = void*;
#else
using StopEventHandle = int;
#endif

void installHandler();

// Requests a stop: sets the flag, signals the event, and (Windows, once a
// handler is installed) arms the force-exit watchdog. Tests: a second request
// while that deadline is armed exits the process — reset() between requests,
// i.e. one testutils::ScopedStopSignalReset per request (per loop iteration or
// section), never one guard for several requests.
void requestStop();

// Clears the flag, the armed force-exit deadline and the event, so a case can
// request a stop again without tripping the watchdog.
void reset();

bool isStopRequested();

auto stopEventHandle() -> StopEventHandle;

// Blocks up to timeout; returns true when a stop has been requested. Wakes
// immediately on a stop request instead of sleeping the full timeout.
bool waitForStop(std::chrono::milliseconds timeout);

inline int canceledExitCodeForPromptAbort() {
  return isStopRequested() ? kCanceledExitCode : 0;
}

#if defined(_WIN32)
// Signature of the force-exit action (defaults to ::ExitProcess).
using ForceExitFn = void(__stdcall*)(unsigned int);

// Test-only hooks: make the force-exit watchdog observable
// without terminating the test process. Restore defaults with nullptr / 3s.
void setForceExitGracePeriodForTest(std::chrono::milliseconds gracePeriod);

void setForceExitHandlerForTest(ForceExitFn handler);
#endif

}  // namespace stopsignal
