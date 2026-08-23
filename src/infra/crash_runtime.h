#pragma once

#include <exception>
#include <string>
#include <string_view>

namespace crash {

void installHandlers();

void reportCaughtException(std::string_view context, std::exception const& exception);

void reportUnknownException(std::string_view context);

// Appends a single formatted line ([timestamp] [critical] [infra.crash] message)
// directly to the current log file, bypassing the async queue; when JSON
// logging is active, a companion NDJSON record is written best-effort too
// (may be absent). Returns false if no log file is active or the write fails
// after bounded retries.
bool writeDirectLogLine(std::string_view message);

// Builds a single-line NDJSON crash record matching the logging schema:
// {timestamp (UTC .sssZ), level=critical, module=infra.crash, message (escaped,
// may contain line breaks), run_id}. Pure and lock-free; used by the crash
// handler's direct write when JSON logging is active (D8).
[[nodiscard]] auto formatCrashJsonLine(std::string_view message, std::string_view runId)
  -> std::string;

}  // namespace crash
