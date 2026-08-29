#include "core/job_state_detail.h"

#include "core/work_dirs.h"

#include "logging/log_tags.h"
#include "logging/logging.h"
#include "logging/setup.h"

#include <utility>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::CORE_JOB);

namespace jobstate {

Store::Store(fs::path stateFilePath): stateFilePath_(std::move(stateFilePath)) { }

auto Store::stateFilePath() const -> fs::path const& {
  return stateFilePath_;
}

auto Store::currentJobId() const -> std::string {
  auto lock = std::scoped_lock{mtx_};
  return snapshot_.jobId;
}

// Restore path: load the snapshot and adopt it when the config matches.
// Returns true when restored; false when the caller must create a fresh
// snapshot; error when the load or persist fails.
auto Store::tryRestoreExistingLocked(
  ConfigSnapshot const& currentConfig,
  bool resumeState,
  bool* discardedMismatched
) -> eh::Result<bool> {
  auto const loaded = detail::loadSnapshot(stateFilePath_);
  if (!loaded) { return eh::makeError("{}", loaded.error()); }

  if (!configMatches(loaded->config, currentConfig)) {
    if (resumeState) {
      return eh::makeError(
        "State file does not match current command: {}",
        stateFilePath_.string()
      );
    }
    if (discardedMismatched != nullptr) { *discardedMismatched = true; }
    return false;
  }

  snapshot_ = loaded.value();
  snapshot_.config = currentConfig;
  snapshot_.cancelRequested = false;
  snapshot_.updatedAtMs = detail::nowMs();
  for (auto& task: snapshot_.tasks) { detail::normalizeExistingTask(task); }
  rebuildIndexLocked();
  // D3: align logging run id with the restored job so log records
  // emitted after resume cross-reference the state file. A corrupted file
  // missing the jobId gets a fresh id rather than an empty one.
  if (snapshot_.jobId.empty()) { snapshot_.jobId = logging::runId(); }
  logging::setRunId(snapshot_.jobId);
  if (!flushLocked(true)) {
    return eh::makeError(
      "Failed to persist loaded job state: {}",
      stateFilePath_.string()
    );
  }
  return true;
}

auto Store::initialize(
  appctx::AppConfig const& config,
  bool restart,
  bool* discardedMismatched
) -> eh::Result<bool> {
  auto lock = std::scoped_lock{mtx_};
  if (discardedMismatched != nullptr) { *discardedMismatched = false; }
  auto ec = std::error_code{};
  fs::create_directories(stateFilePath_.parent_path(), ec);
  workdirs::setHiddenOnEncroDir(stateFilePath_.parent_path());

  auto const currentConfig = buildConfigSnapshot(config);
  auto const stateExists = fs::exists(stateFilePath_, ec) && !ec;
  if (!restart && !stateExists && config.resumeState) {
    return eh::makeError(
      "Resume requested but no state file was found: {}",
      stateFilePath_.string()
    );
  }

  if (!restart && stateExists) {
    auto const restored =
      tryRestoreExistingLocked(currentConfig, config.resumeState, discardedMismatched);
    if (!restored) { return restored; }
    if (
      restored.value()
    ) {  // NOLINT(bugprone-unchecked-optional-access): guarded by the !restored check above
      return true;
    }
  }

  snapshot_ = Snapshot{
    .version = detail::kStateVersion,
    .jobId = logging::runId(),
    .stage = "planning",
    .cancelRequested = false,
    .updatedAtMs = detail::nowMs(),
    .config = currentConfig,
    .tasks = {},
  };
  rebuildIndexLocked();
  if (!flushLocked(true)) {
    return eh::makeError(
      "Failed to persist initial job state: {}",
      stateFilePath_.string()
    );
  }
  return false;
}

auto Store::mergeTasks(std::span<TaskRecord const> plannedTasks)
  -> std::vector<TaskRecord> {
  auto lock = std::scoped_lock{mtx_};
  auto mergedTasks = std::vector<TaskRecord>{};
  mergedTasks.reserve(plannedTasks.size());

  for (auto const& plannedTask: plannedTasks) {
    if (auto const index = indexFor(plannedTask.id); index.has_value()) {
      auto& existing = snapshot_.tasks[index.value()];

      auto preserved = existing;
      auto const planFingerprintChanged = existing.fingerprint != plannedTask.fingerprint;
      preserved.kind = plannedTask.kind;
      preserved.label = plannedTask.label;
      preserved.fingerprint = plannedTask.fingerprint;
      preserved.sourcePaths = plannedTask.sourcePaths;
      preserved.targetPaths = plannedTask.targetPaths;

      if (planFingerprintChanged) {
        detail::clearExecutionState(preserved);
      } else {
        detail::normalizeExistingTask(preserved);
      }

      existing = std::move(preserved);
      mergedTasks.push_back(existing);
      continue;
    }

    auto task = plannedTask;
    task.updatedAtMs = detail::nowMs();
    snapshot_.tasks.push_back(std::move(task));
    taskIndex_[snapshot_.tasks.back().id] = snapshot_.tasks.size() - 1;
    mergedTasks.push_back(snapshot_.tasks.back());
  }

  snapshot_.updatedAtMs = detail::nowMs();
  // Persistence failure is logged inside persistLocked; mergeTasks itself
  // still returns the merged task list.
  (void)persistLocked("mergeTasks", true);
  return mergedTasks;
}

auto Store::tasks() const -> std::vector<TaskRecord> {
  auto lock = std::scoped_lock{mtx_};
  return snapshot_.tasks;
}

auto Store::findTask(std::string_view id) const -> std::optional<TaskRecord> {
  auto lock = std::scoped_lock{mtx_};
  auto const index = indexFor(id);
  if (!index.has_value()) { return std::nullopt; }
  return snapshot_.tasks[index.value()];
}

void Store::setStage(std::string_view stage) {
  auto lock = std::scoped_lock{mtx_};
  snapshot_.stage = std::string{stage};
  snapshot_.updatedAtMs = detail::nowMs();
  persistLocked("setStage", true);
}

void Store::requestCancel() {
  auto lock = std::scoped_lock{mtx_};
  snapshot_.cancelRequested = true;
  snapshot_.stage = "canceling";
  snapshot_.updatedAtMs = detail::nowMs();
  persistLocked("requestCancel", true);
}

bool Store::isCancelRequested() const {
  auto lock = std::scoped_lock{mtx_};
  return snapshot_.cancelRequested;
}

void Store::markRunning(std::string_view id) {
  auto lock = std::scoped_lock{mtx_};
  auto const index = indexFor(id);
  if (!index.has_value()) { return; }

  auto& task = snapshot_.tasks[index.value()];
  // No settle here: the previous attempt already settled at its terminal
  // transition, so anything between then and now is idle time that the
  // accumulated total must not count.
  task.status = TaskStatus::Running;
  task.attemptCount += 1;
  task.startedAtMs = detail::nowMs();
  task.updatedAtMs = task.startedAtMs;
  task.finishedAtMs.reset();
  task.lastError.reset();
  persistLocked("markRunning", true);
}

void Store::markProgress(
  std::string_view id,
  std::optional<float> progress,
  std::optional<std::uint64_t> frameCount,
  std::optional<std::string_view> status
) {
  auto lock = std::scoped_lock{mtx_};
  auto const index = indexFor(id);
  if (!index.has_value()) { return; }

  auto& task = snapshot_.tasks[index.value()];
  if (progress.has_value()) { task.lastProgress = progress; }
  if (frameCount.has_value()) { task.lastFrameCount = frameCount; }
  if (status.has_value()) { task.lastStatus = std::string{status.value()}; }
  task.updatedAtMs = detail::nowMs();
  snapshot_.updatedAtMs = task.updatedAtMs.value();
  persistLocked("markProgress", false);
}

void Store::markSegmentProgress(
  std::string_view id,
  std::uint64_t segmentIndex,
  std::uint64_t resumeTimeUs
) {
  auto lock = std::scoped_lock{mtx_};
  auto const index = indexFor(id);
  if (!index.has_value()) { return; }

  auto& task = snapshot_.tasks[index.value()];
  task.segmentIndex = segmentIndex;
  task.resumeTimeUs = resumeTimeUs;
  task.updatedAtMs = detail::nowMs();
  snapshot_.updatedAtMs = task.updatedAtMs.value();
  persistLocked("markSegmentProgress", true);
}

void Store::markSucceeded(std::string_view id, std::optional<std::string_view> status) {
  auto lock = std::scoped_lock{mtx_};
  auto const index = indexFor(id);
  if (!index.has_value()) { return; }

  auto& task = snapshot_.tasks[index.value()];
  settleEncodedMs(task, detail::nowMs());
  task.status = TaskStatus::Succeeded;
  task.lastProgress = 100.0f;
  if (status.has_value()) { task.lastStatus = std::string{status.value()}; }
  task.lastError.reset();
  task.finishedAtMs = detail::nowMs();
  task.updatedAtMs = task.finishedAtMs;
  snapshot_.updatedAtMs = task.finishedAtMs.value();
  persistLocked("markSucceeded", true);
}

void Store::markFailed(std::string_view id, std::string_view error) {
  auto lock = std::scoped_lock{mtx_};
  auto const index = indexFor(id);
  if (!index.has_value()) { return; }

  auto& task = snapshot_.tasks[index.value()];
  settleEncodedMs(task, detail::nowMs());
  task.status = TaskStatus::Failed;
  task.lastError = std::string{error};
  task.finishedAtMs = detail::nowMs();
  task.updatedAtMs = task.finishedAtMs;
  snapshot_.updatedAtMs = task.finishedAtMs.value();
  persistLocked("markFailed", true);
}

void Store::markInterrupted(std::string_view id, std::string_view reason) {
  auto lock = std::scoped_lock{mtx_};
  auto const index = indexFor(id);
  if (!index.has_value()) { return; }

  auto& task = snapshot_.tasks[index.value()];
  if (task.status == TaskStatus::Succeeded || task.status == TaskStatus::Failed) {
    return;
  }
  settleEncodedMs(task, detail::nowMs());

  task.status = TaskStatus::Interrupted;
  if (!reason.empty()) { task.lastError = std::string{reason}; }
  task.finishedAtMs = detail::nowMs();
  task.updatedAtMs = task.finishedAtMs;
  snapshot_.updatedAtMs = task.finishedAtMs.value();
  persistLocked("markInterrupted", true);
}

void Store::markIncompleteInterrupted(
  std::span<std::string const> ids,
  std::string_view reason
) {
  auto lock = std::scoped_lock{mtx_};
  auto changed = false;
  auto const now = detail::nowMs();
  for (auto const& id: ids) {
    auto const index = indexFor(id);
    if (!index.has_value()) { continue; }

    auto& task = snapshot_.tasks[index.value()];
    if (task.status == TaskStatus::Pending || task.status == TaskStatus::Running) {
      settleEncodedMs(task, now);
      task.status = TaskStatus::Interrupted;
      task.lastError = std::string{reason};
      task.finishedAtMs = now;
      task.updatedAtMs = now;
      changed = true;
    }
  }

  if (!changed) { return; }

  snapshot_.updatedAtMs = now;
  snapshot_.stage = "canceled";
  snapshot_.cancelRequested = true;
  persistLocked("markIncompleteInterrupted", true);
}

void Store::flush() {
  auto lock = std::scoped_lock{mtx_};
  persistLocked("flush", true);
}

auto Store::indexFor(std::string_view id) const -> std::optional<std::size_t> {
  if (auto const it = taskIndex_.find(std::string{id}); it != taskIndex_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void Store::rebuildIndexLocked() {
  taskIndex_.clear();
  for (auto index = std::size_t{0}; index < snapshot_.tasks.size(); ++index) {
    taskIndex_[snapshot_.tasks[index].id] = index;
  }
}

void Store::persistLocked(std::string_view operation, bool force) {
  auto const result = flushLocked(force);
  if (!result) {
    // A failed persistence must never vanish silently.
    LOG_ERROR("Failed to persist job state after {}: {}", operation, result.error());
  }
}

auto Store::flushLocked(bool force) -> eh::Result<void> {
  return detail::flushSnapshot(stateFilePath_, snapshot_, lastFlushAtMs_, force);
}

}  // namespace jobstate
