#pragma once

namespace stopsignal {

constexpr auto kCanceledExitCode = 130;

void installHandler();

void requestStop();

void reset();

auto isStopRequested() -> bool;

inline auto canceledExitCodeForPromptAbort() -> int {
  return isStopRequested() ? kCanceledExitCode : 0;
}

}  // namespace stopsignal
