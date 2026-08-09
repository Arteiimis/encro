#pragma once

#include <chrono>

namespace stopsignal {

constexpr auto kCanceledExitCode = 130;

void installHandler();

void requestStop();

void reset();

auto isStopRequested() -> bool;

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
