#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace batch {

namespace fs = std::filesystem;

// One whole batch run: the root to enumerate, the packed output root, the
// processing options every subfolder shares, and the toolchain resolved once
// for the run.
struct BatchRequest {
  fs::path root;
  fs::path packedRoot;
  appctx::AppConfig baseConfig;
  appctx::ToolchainPaths toolchain;
};

enum class TaskOutcome {
  Done,
  Failed,
  Skipped,
};

struct SubTaskResult {
  fs::path subfolder;
  TaskOutcome outcome = TaskOutcome::Skipped;
  int exitCode = 0;
  std::string error;
};

// First-level subdirectories of `root` that are batch tasks: directories only,
// sorted by name, excluding hidden entries (dot-prefixed, which covers the
// .encro work root) and the packed output root wherever it resolves to.
auto enumerateSubfolders(fs::path const& root, fs::path const& packedRoot)
  -> eh::Result<std::vector<fs::path>>;

// True when `dir` holds at least one file the effective option combination
// would process: video encode → video extensions; picture → picture
// extensions (plus videos when the run converts them); pack-only → any regular
// file. Mirrors the pipeline's own scans so a subfolder is skipped exactly
// when its single-directory run would find nothing to do.
auto hasProcessableMedia(appctx::AppConfig const& config, fs::path const& dir) -> bool;

// The per-subfolder config: the batch's options with the subfolder as the
// input, its archives redirected under the packed root, and no batch-level
// output or state path.
auto subConfigFor(BatchRequest const& request, fs::path const& subfolder)
  -> appctx::AppConfig;

// The packed root a batch run writes under: the explicit -o override, or
// <root>/packed by default.
auto resolvePackedRoot(
  fs::path const& root,
  std::optional<fs::path> const& outputOverride
) -> fs::path;

// Runs the enumerated subfolders in order, each through the single-directory
// pipeline. A media-less subfolder is skipped with a notice; a failed
// subfolder does not stop the run; a stop request stops the iteration, leaving
// the remaining subfolders skipped.
auto runTasks(BatchRequest const& request) -> eh::Result<std::vector<SubTaskResult>>;

// The batch exit code for a finished run: the cancellation code when a stop
// was requested, non-zero when any subfolder failed, zero otherwise.
auto batchExitCode(std::span<SubTaskResult const> results) -> int;

// The product-facing summary table: one row per enumerated subfolder with its
// status, then the count line (stdout, console-output-conventions).
void printSummary(std::span<SubTaskResult const> results);

// The whole batch: run every subfolder, print the summary table (a notice when
// the root has no eligible subfolder), and return the batch exit code.
auto runBatch(BatchRequest const& request) -> eh::Result<int>;

}  // namespace batch
