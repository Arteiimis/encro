#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace logging {

// Bootstrap run id: lazily generated UUID before setup(), regenerated in
// setup(); job-state adopt it as the fresh jobId (D3).
[[nodiscard]] auto runId() -> std::string;
auto setRunId(std::string id) -> void;

// Lock-free snapshot of the current run id, safe to call from crash handlers
// (never takes a lock). Returns either the previous or the new id, atomically;
// empty before the first set or after shutdown().
[[nodiscard]] auto runIdSnapshot() -> std::string_view;

// ── End-of-run summary (D6) ─────────────────────────────────────────────────
// One summary record per run, emitted through the normal logger; the NDJSON
// formatter turns it into a `summary` object (log path and level_counts are
// attached there), the human sink shows the RUN SUMMARY: line as-is.

struct SummaryData {
  std::string status;  // success | failed | interrupted
  std::optional<std::string> jobId;
  std::optional<std::size_t> tasksTotal;
  std::optional<std::size_t> tasksFailed;
  std::optional<std::int64_t> elapsedMs;
};

auto logRunSummary(SummaryData const& data) -> void;

// Level name -> record count, accumulated by the pass-through counting sink.
[[nodiscard]] auto levelCounts() -> std::map<std::string, std::uint64_t>;

// Current .ndjson companion path (present only when JSON logging is active),
// for crash-handler direct writes (D8).
[[nodiscard]] auto currentNdjsonFilePath() -> std::optional<std::filesystem::path>;

struct LogConfig {
  bool echoEnabled{false};
  bool jsonEnabled{false};
  bool colorsEnabled{true};
  std::optional<std::filesystem::path> customLogDir;
};

// ── Environment snapshot data for forensic diagnostics ──────────────────────
// Populated via updateForensicSnapshot (production) or setForensicSnapshotData (tests).

struct EnvironmentSnapshot {
  std::string pipelineType{"unknown"};
  int activeSlots{0};
  int totalSlots{0};
  int pending{0};
  int finished{0};
  std::optional<int> subprocessPid;
  std::optional<std::string> subprocessCmdline;
  bool hasEncodingContext{false};
};

// Initialize the logging system: create shared sinks, register 24 named
// async_loggers, set the default logger.
// Returns the created log file path (std::nullopt if no log file could be created).
[[nodiscard]] auto setup(LogConfig const& config) -> std::optional<std::filesystem::path>;

// Teardown: flush + shut down all loggers
auto shutdown() -> void;

// Returns the currently active log file path (D-13: crash handler integration).
// Returns std::nullopt if setup() was not called or no file sink is active.
[[nodiscard]] auto currentLogFilePath() -> std::optional<std::filesystem::path>;

// ── Forensic context ────────────────────────────────────────────────────────
// Store app context pointer for environment snapshot access.
auto setForensicAppContext(void* appCtx) -> void;

// Update the forensic snapshot with live encoding progress (called from monitor thread).
auto updateForensicSnapshot(int activeSlots, int totalSlots, int pending, int finished)
  -> void;

// Test-only: directly set snapshot data for test verification.
auto setForensicSnapshotData(EnvironmentSnapshot const& data) -> void;

// Test-only: clear all forensic state.
auto clearForensicSnapshotData() -> void;

// Capture a lock-free environment snapshot. Returns "" when no AppContext is set.
// Returns a minimal snapshot when no encoding context is active.
// Returns a detailed snapshot (active slots, pending, subprocess info) when encoding.
[[nodiscard]] auto captureEnvironmentSnapshot() -> std::string;

}  // namespace logging
