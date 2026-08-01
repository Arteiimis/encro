#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace jobstate {

namespace fs = std::filesystem;

inline constexpr auto kEncodeVideoKind = std::string_view{"encode_video"};
inline constexpr auto kBuildArchiveKind = std::string_view{"build_archive"};

enum class TaskStatus {
  Pending,
  Running,
  Succeeded,
  Failed,
  Interrupted,
};

struct ConfigSnapshot {
  std::string processType;
  std::string outputFormat;
  std::string outputLayout;
  bool packOutput = false;
  bool packOnly = false;
  bool recursive = false;
  bool forceNameConflictHandling = true;
  bool pictureFolderSummary = false;
  std::vector<fs::path> inputPaths;
  std::optional<fs::path> outputPath;
};

struct TaskRecord {
  std::string id;
  std::string kind = std::string{kEncodeVideoKind};
  TaskStatus status = TaskStatus::Pending;
  std::string label;
  std::size_t attemptCount = 0;
  std::string fingerprint;
  std::vector<fs::path> sourcePaths;
  std::vector<fs::path> targetPaths;
  std::optional<float> lastProgress;
  std::optional<std::uint64_t> lastFrameCount;
  std::optional<std::string> lastStatus;
  std::optional<std::string> lastError;
  std::optional<std::int64_t> startedAtMs;
  std::optional<std::int64_t> updatedAtMs;
  std::optional<std::int64_t> finishedAtMs;
  std::optional<std::uint64_t> segmentIndex;
  std::optional<std::uint64_t> resumeTimeUs;
};

struct Snapshot {
  int version = 1;
  std::string jobId;
  std::string stage = "planning";
  bool cancelRequested = false;
  std::int64_t updatedAtMs = 0;
  ConfigSnapshot config;
  std::vector<TaskRecord> tasks;
};

class Store {
public:
  explicit Store(fs::path stateFilePath);

  auto stateFilePath() const -> fs::path const&;

  auto initialize(appctx::AppConfig const& config, bool restart) -> eh::Result<bool>;

  auto mergeTasks(std::span<TaskRecord const> plannedTasks) -> std::vector<TaskRecord>;

  auto tasks() const -> std::vector<TaskRecord>;

  auto findTask(std::string_view id) const -> std::optional<TaskRecord>;

  void setStage(std::string_view stage);

  void requestCancel();

  auto isCancelRequested() const -> bool;

  void markRunning(std::string_view id);

  void markProgress(
    std::string_view id,
    std::optional<float> progress = std::nullopt,
    std::optional<std::uint64_t> frameCount = std::nullopt,
    std::optional<std::string_view> status = std::nullopt
  );

  void markSegmentProgress(
    std::string_view id,
    std::uint64_t segmentIndex,
    std::uint64_t resumeTimeUs
  );

  void markSucceeded(
    std::string_view id,
    std::optional<std::string_view> status = std::nullopt
  );

  void markFailed(std::string_view id, std::string_view error);

  void markInterrupted(std::string_view id, std::string_view reason = {});

  void markIncompleteInterrupted(
    std::span<std::string const> ids,
    std::string_view reason = "canceled by user"
  );

  void flush();

private:
  auto indexFor(std::string_view id) const -> std::optional<std::size_t>;

  void rebuildIndexLocked();

  void flushLocked(bool force);

  fs::path stateFilePath_;
  Snapshot snapshot_;
  std::unordered_map<std::string, std::size_t> taskIndex_;
  mutable std::mutex mtx_;
  std::int64_t lastFlushAtMs_ = 0;
};

auto buildDefaultStateFilePath(appctx::AppConfig const& config) -> fs::path;

auto buildConfigSnapshot(appctx::AppConfig const& config) -> ConfigSnapshot;

auto configMatches(ConfigSnapshot const& lhs, ConfigSnapshot const& rhs) -> bool;

auto makeEncodeTask(fs::path const& inputPath, fs::path const& plannedOutputFile)
  -> TaskRecord;

auto makeArchiveTask(
  fs::path const& archiveFile,
  std::span<fs::path const> members,
  std::string label
) -> TaskRecord;

auto primarySourcePath(TaskRecord const& task) -> std::optional<fs::path>;

auto primaryTargetPath(TaskRecord const& task) -> std::optional<fs::path>;

auto needsExecution(TaskRecord const& task) -> bool;

auto actionTargetExists(TaskRecord const& task) -> bool;

}  // namespace jobstate
