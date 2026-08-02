#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace logging {

struct LogConfig {
  bool verboseEnabled{false};
  bool verboseEchoEnabled{false};
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
// Returns the created log file path (std::nullopt if logging is disabled).
[[nodiscard]] auto setup(LogConfig const& config) -> std::optional<std::filesystem::path>;

// Teardown: flush + shut down all loggers
auto shutdown() -> void;

// Returns the currently active log file path (D-13: crash handler integration).
// Returns std::nullopt if setup() was not called or verbose logging is disabled.
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
