#pragma once

#include <exception>
#include <functional>
#include <string>
#include <string_view>

namespace crash {

void installHandlers();

// Installs (or clears with nullptr) a provider consulted when a crash record is
// written; its return value is appended to the record's reason line as
// " [context: ...]" (e.g. the running test's name). Intended to be set once
// during startup, before any crash can occur. A throwing provider is silently
// ignored — the crash path must never fail inside itself.
void setCrashContextProvider(std::function<std::string()> provider);

// Marks the current thread as inside a window where a DLL load may be running
// its initialization on this thread (loader lock possibly held). Crash paths
// consult inDllLoadZone() to skip report work that can block there — the
// dbgeng-backed stack capture waits forever under loader lock. Nestable; the
// zone exists on every platform but is inert without the Windows VEH.
class ScopedDllLoadZone {
public:
  ScopedDllLoadZone();
  ~ScopedDllLoadZone();
  ScopedDllLoadZone(ScopedDllLoadZone const&) = delete;
  auto operator=(ScopedDllLoadZone const&) = delete;
};

// True while the current thread is inside a ScopedDllLoadZone. Lock-free and
// safe to call from crash handlers.
bool inDllLoadZone();

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
