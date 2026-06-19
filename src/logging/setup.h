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

// 初始化日志系统: 创建共享 sink、注册 24 个 named async_logger、设置 default_logger。
// 返回创建的日志文件路径 (std::nullopt 如果 logging 未启用)。
[[nodiscard]] auto setup(LogConfig const& config) -> std::optional<std::filesystem::path>;

// 销毁: flush + 关闭所有 logger
auto shutdown() -> void;

// 返回当前活跃的日志文件路径 (D-13: crash handler 集成)
// 如果 setup() 未调用或 verbose 未启用，返回 std::nullopt
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
