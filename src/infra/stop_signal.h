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

void requestStop();

void reset();

auto isStopRequested() -> bool;

auto stopEventHandle() -> StopEventHandle;

// Blocks up to timeout; returns true when a stop has been requested. Wakes
// immediately on a stop request instead of sleeping the full timeout.
auto waitForStop(std::chrono::milliseconds timeout) -> bool;

inline auto canceledExitCodeForPromptAbort() -> int {
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
