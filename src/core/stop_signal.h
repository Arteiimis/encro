#pragma once

namespace stopsignal {

constexpr auto kCanceledExitCode = 130;

void installHandler();

void requestStop();

void reset();

auto isStopRequested() -> bool;

}  // namespace stopsignal
