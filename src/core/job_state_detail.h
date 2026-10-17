#pragma once

#include "core/job_state.h"

namespace jobstate::detail {

inline constexpr auto kStateVersion = 1;

std::int64_t nowMs();

auto outputLayoutToString(appctx::OutputLayout layout) -> std::string;

auto loadSnapshot(fs::path const& path) -> eh::Result<Snapshot>;

void clearExecutionState(TaskRecord& task);

void normalizeExistingTask(TaskRecord& task);

// Moves the staged snapshot over the state file without ever emptying it.
auto replaceStateFile(fs::path const& stagedPath, fs::path const& stateFilePath)
  -> eh::Result<void>;

auto flushSnapshot(
  fs::path const& stateFilePath,
  Snapshot& snapshot,
  std::int64_t& lastFlushAtMs,
  bool force
) -> eh::Result<void>;

}  // namespace jobstate::detail
