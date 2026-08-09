#pragma once

#include <exception>
#include <string_view>

namespace crash {

void installHandlers();

void reportCaughtException(std::string_view context, std::exception const& exception);

void reportUnknownException(std::string_view context);

// Appends a single formatted line ([timestamp] [critical] [infra.crash] message)
// directly to the current log file, bypassing the async queue. Returns false if
// no log file is active or the write fails after bounded retries.
auto writeDirectLogLine(std::string_view message) -> bool;

}  // namespace crash
