#include "core/job_state.h"
#include "core/job_state_detail.h"

#include "core/collision_naming.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <format>
#include <numeric>
#include <utility>

namespace jobstate {

namespace json = boost::json;
using namespace std::chrono;

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::CORE_JOB);

namespace detail {

constexpr auto kFlushIntervalMs = std::int64_t{2000};

auto nowMs() -> std::int64_t {
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

auto toPathStrings(std::span<fs::path const> paths) -> std::vector<std::string> {
  auto values = std::vector<std::string>{};
  values.reserve(paths.size());
  for (auto const& path: paths) {
    values.push_back(path.lexically_normal().generic_string());
  }
  return values;
}

auto inputWriteTime(fs::path const& path) -> std::optional<std::int64_t> {
  auto ec = std::error_code{};
  if (!fs::exists(path, ec) || ec) { return std::nullopt; }
  return static_cast<std::int64_t>(  //
    fs::last_write_time(path, ec).time_since_epoch().count()
  );
}

auto inputSize(fs::path const& path) -> std::optional<std::uintmax_t> {
  auto ec = std::error_code{};
  if (!fs::exists(path, ec) || ec) { return std::nullopt; }
  if (!fs::is_regular_file(path, ec) || ec) { return std::nullopt; }
  return fs::file_size(path, ec);
}

auto makeTempStatePath(fs::path const& path) -> fs::path {
  return path.parent_path() / std::format("{}.tmp", path.filename().string());
}

auto outputLayoutToString(appctx::OutputLayout layout) -> std::string {
  return layout == appctx::OutputLayout::Keep ? "keep" : "flat";
}

auto taskStatusToString(TaskStatus status) -> std::string_view {
  switch (status) {
    case TaskStatus::Pending    : return "pending";
    case TaskStatus::Running    : return "running";
    case TaskStatus::Succeeded  : return "succeeded";
    case TaskStatus::Failed     : return "failed";
    case TaskStatus::Interrupted: return "interrupted";
  }

  return "pending";
}

auto parseTaskStatus(std::string_view value) -> TaskStatus {
  if (value == "running") { return TaskStatus::Running; }
  if (value == "succeeded") { return TaskStatus::Succeeded; }
  if (value == "failed") { return TaskStatus::Failed; }
  if (value == "interrupted") { return TaskStatus::Interrupted; }
  return TaskStatus::Pending;
}

auto pathToJson(std::optional<fs::path> const& path) -> json::value {
  if (!path.has_value()) { return nullptr; }
  return json::value{path->lexically_normal().generic_string()};
}

auto pathsToJson(std::span<fs::path const> paths) -> json::array {
  auto array = json::array{};
  array.reserve(paths.size());
  for (auto const& path: paths) {
    array.emplace_back(path.lexically_normal().generic_string());
  }
  return array;
}

auto optionalPathFrom(json::object const& object, std::string_view key)
  -> std::optional<fs::path> {
  if (!object.contains(key)) { return std::nullopt; }
  auto const& value = object.at(key);
  if (value.is_null()) { return std::nullopt; }
  if (!value.is_string()) { return std::nullopt; }
  return fs::path{value.as_string().c_str()};
}

auto optionalStringFrom(json::object const& object, std::string_view key)
  -> std::optional<std::string> {
  if (!object.contains(key)) { return std::nullopt; }
  auto const& value = object.at(key);
  if (value.is_null() || !value.is_string()) { return std::nullopt; }
  return std::string{value.as_string().c_str()};
}

template<class Ty>
auto optionalNumberFrom(json::object const& object, std::string_view key)
  -> std::optional<Ty> {
  if (!object.contains(key)) { return std::nullopt; }
  auto const& value = object.at(key);
  if (value.is_null()) { return std::nullopt; }
  if constexpr (std::same_as<Ty, float>) {
    if (value.is_double()) { return static_cast<float>(value.as_double()); }
    if (value.is_int64()) { return static_cast<float>(value.as_int64()); }
    if (value.is_uint64()) { return static_cast<float>(value.as_uint64()); }
    return std::nullopt;
  } else if constexpr (std::unsigned_integral<Ty>) {
    if (value.is_uint64()) { return static_cast<Ty>(value.as_uint64()); }
    if (value.is_int64() && value.as_int64() >= 0) {
      return static_cast<Ty>(value.as_int64());
    }
    return std::nullopt;
  } else {
    if (value.is_int64()) { return static_cast<Ty>(value.as_int64()); }
    if (value.is_uint64()) { return static_cast<Ty>(value.as_uint64()); }
    return std::nullopt;
  }
}

auto optionalStringArrayFrom(json::object const& object, std::string_view key)
  -> std::vector<fs::path> {
  auto values = std::vector<fs::path>{};
  if (!object.contains(key) || !object.at(key).is_array()) { return values; }
  for (auto const& value: object.at(key).as_array()) {
    if (!value.is_string()) { continue; }
    values.emplace_back(fs::path{value.as_string().c_str()});
  }
  return values;
}

auto toFingerprintToken(fs::path const& path) -> std::string {
  return std::format(
    "{}#{}#{}",
    collisionnaming::stablePathString(path),
    inputSize(path)
      .transform([](auto value) { return std::to_string(value); })
      .value_or("missing"),
    inputWriteTime(path)
      .transform([](auto value) { return std::to_string(value); })
      .value_or("missing")
  );
}

auto buildFingerprint(
  std::string_view kind,
  std::span<fs::path const> sourcePaths,
  std::span<fs::path const> targetPaths
) -> std::string {
  auto payload = std::string{kind};
  payload += "|src=";
  for (auto const& path: sourcePaths) {
    payload += toFingerprintToken(path);
    payload += ';';
  }
  payload += "|dst=";
  for (auto const& path: targetPaths) {
    payload += collisionnaming::stablePathString(path);
    payload += ';';
  }
  return collisionnaming::shortPathHash(payload);
}

auto legacySourcePathsFrom(json::object const& object) -> std::vector<fs::path> {
  auto sourcePaths = optionalStringArrayFrom(object, "archiveMembers");
  if (!sourcePaths.empty()) { return sourcePaths; }

  if (
    auto const inputPath = optionalPathFrom(object, "inputPath"); inputPath.has_value()
  ) {
    sourcePaths.push_back(inputPath.value());
  }

  return sourcePaths;
}

auto legacyTargetPathsFrom(json::object const& object) -> std::vector<fs::path> {
  auto targetPaths = std::vector<fs::path>{};

  if (
    auto const plannedOutputFile = optionalPathFrom(object, "plannedOutputFile");
    plannedOutputFile.has_value()
  ) {
    targetPaths.push_back(plannedOutputFile.value());
    return targetPaths;
  }

  if (
    auto const archiveFile = optionalPathFrom(object, "archiveFile");
    archiveFile.has_value()
  ) {
    targetPaths.push_back(archiveFile.value());
  }

  return targetPaths;
}

auto inferLegacyKind(
  json::object const& object,
  std::vector<fs::path> const& sourcePaths,
  std::vector<fs::path> const& targetPaths
) -> std::string {
  if (auto const kind = optionalStringFrom(object, "kind"); kind.has_value()) {
    return kind.value();
  }

  if (object.contains("archiveFile") || sourcePaths.size() > 1) {
    return std::string{kBuildArchiveKind};
  }

  if (!targetPaths.empty() || !sourcePaths.empty()) {
    return std::string{kEncodeVideoKind};
  }

  return std::string{kEncodeVideoKind};
}

auto currentFingerprint(TaskRecord const& task) -> std::string {
  return buildFingerprint(task.kind, task.sourcePaths, task.targetPaths);
}

auto toJson(ConfigSnapshot const& config) -> json::object {
  auto object = json::object{};
  object["processType"] = config.processType;
  object["outputFormat"] = config.outputFormat;
  object["outputLayout"] = config.outputLayout;
  object["packOutput"] = config.packOutput;
  object["packOnly"] = config.packOnly;
  object["recursive"] = config.recursive;
  object["forceNameConflictHandling"] = config.forceNameConflictHandling;
  object["pictureFolderSummary"] = config.pictureFolderSummary;
  object["inputPaths"] = pathsToJson(config.inputPaths);
  object["outputPath"] = pathToJson(config.outputPath);
  return object;
}

auto fromJsonConfig(json::object const& object) -> ConfigSnapshot {
  return ConfigSnapshot{
    .processType = optionalStringFrom(object, "processType").value_or("video"),
    .outputFormat = optionalStringFrom(object, "outputFormat").value_or("mp4"),
    .outputLayout = optionalStringFrom(object, "outputLayout").value_or("flat"),
    .packOutput = object.if_contains("packOutput") && object.at("packOutput").as_bool(),
    .packOnly = object.if_contains("packOnly") && object.at("packOnly").as_bool(),
    .recursive = object.if_contains("recursive") && object.at("recursive").as_bool(),
    .forceNameConflictHandling = !object.if_contains("forceNameConflictHandling")
      || object.at("forceNameConflictHandling").as_bool(),
    .pictureFolderSummary = object.if_contains("pictureFolderSummary")
      && object.at("pictureFolderSummary").as_bool(),
    .inputPaths = optionalStringArrayFrom(object, "inputPaths"),
    .outputPath = optionalPathFrom(object, "outputPath"),
  };
}

template<class Ty>
auto jsonOrNull(std::optional<Ty> const& value) -> json::value {
  return value.has_value() ? json::value(*value) : json::value(nullptr);
}

auto toJson(TaskRecord const& task) -> json::object {
  auto object = json::object{};
  object["id"] = task.id;
  object["kind"] = task.kind;
  object["status"] = taskStatusToString(task.status);
  object["label"] = task.label;
  object["attemptCount"] = task.attemptCount;
  object["fingerprint"] = task.fingerprint;
  object["sourcePaths"] = pathsToJson(task.sourcePaths);
  object["targetPaths"] = pathsToJson(task.targetPaths);
  object["lastProgress"] = jsonOrNull(task.lastProgress);
  object["lastFrameCount"] = jsonOrNull(task.lastFrameCount);
  object["lastStatus"] = jsonOrNull(task.lastStatus);
  object["lastError"] = jsonOrNull(task.lastError);
  object["startedAtMs"] = jsonOrNull(task.startedAtMs);
  object["updatedAtMs"] = jsonOrNull(task.updatedAtMs);
  object["finishedAtMs"] = jsonOrNull(task.finishedAtMs);
  object["segmentIndex"] = jsonOrNull(task.segmentIndex);
  object["resumeTimeUs"] = jsonOrNull(task.resumeTimeUs);
  return object;
}

auto fromJsonTask(json::object const& object) -> TaskRecord {
  auto sourcePaths = optionalStringArrayFrom(object, "sourcePaths");
  if (sourcePaths.empty()) { sourcePaths = legacySourcePathsFrom(object); }

  auto targetPaths = optionalStringArrayFrom(object, "targetPaths");
  if (targetPaths.empty()) { targetPaths = legacyTargetPathsFrom(object); }

  auto kind = inferLegacyKind(object, sourcePaths, targetPaths);
  auto fingerprint = optionalStringFrom(object, "fingerprint");

  return TaskRecord{
    .id = optionalStringFrom(object, "id").value_or(std::string{}),
    .kind = std::move(kind),
    .status = parseTaskStatus(optionalStringFrom(object, "status").value_or("pending")),
    .label = optionalStringFrom(object, "label").value_or(std::string{}),
    .attemptCount = optionalNumberFrom<std::size_t>(object, "attemptCount").value_or(0),
    .fingerprint = fingerprint.value_or(buildFingerprint(
      inferLegacyKind(object, sourcePaths, targetPaths),
      sourcePaths,
      targetPaths
    )),
    .sourcePaths = std::move(sourcePaths),
    .targetPaths = std::move(targetPaths),
    .lastProgress = optionalNumberFrom<float>(object, "lastProgress"),
    .lastFrameCount = optionalNumberFrom<std::uint64_t>(object, "lastFrameCount"),
    .lastStatus = optionalStringFrom(object, "lastStatus"),
    .lastError = optionalStringFrom(object, "lastError"),
    .startedAtMs = optionalNumberFrom<std::int64_t>(object, "startedAtMs"),
    .updatedAtMs = optionalNumberFrom<std::int64_t>(object, "updatedAtMs"),
    .finishedAtMs = optionalNumberFrom<std::int64_t>(object, "finishedAtMs"),
    .segmentIndex = optionalNumberFrom<std::uint64_t>(object, "segmentIndex"),
    .resumeTimeUs = optionalNumberFrom<std::uint64_t>(object, "resumeTimeUs"),
  };
}

auto toJson(Snapshot const& snapshot) -> json::object {
  auto object = json::object{};
  object["version"] = snapshot.version;
  object["jobId"] = snapshot.jobId;
  object["stage"] = snapshot.stage;
  object["cancelRequested"] = snapshot.cancelRequested;
  object["updatedAtMs"] = snapshot.updatedAtMs;
  object["config"] = toJson(snapshot.config);

  auto tasks = json::array{};
  tasks.reserve(snapshot.tasks.size());
  for (auto const& task: snapshot.tasks) { tasks.emplace_back(toJson(task)); }
  object["tasks"] = std::move(tasks);
  return object;
}

auto fromJsonSnapshot(json::object const& object) -> Snapshot {
  auto snapshot = Snapshot{};
  snapshot.version = optionalNumberFrom<int>(object, "version").value_or(kStateVersion);
  snapshot.jobId = optionalStringFrom(object, "jobId").value_or(std::string{});
  snapshot.stage = optionalStringFrom(object, "stage").value_or("planning");
  snapshot.cancelRequested =
    object.if_contains("cancelRequested") && object.at("cancelRequested").as_bool();
  snapshot.updatedAtMs =
    optionalNumberFrom<std::int64_t>(object, "updatedAtMs").value_or(0);
  if (object.if_contains("config") && object.at("config").is_object()) {
    snapshot.config = fromJsonConfig(object.at("config").as_object());
  }
  auto const* storedTasks = object.if_contains("tasks");
  auto const* legacyActions = object.if_contains("actions");
  auto const* taskArray = storedTasks != nullptr && storedTasks->is_array() ? storedTasks
    : legacyActions != nullptr && legacyActions->is_array() ? legacyActions
                                                            : nullptr;
  if (taskArray != nullptr) {
    for (auto const& task: taskArray->as_array()) {
      if (!task.is_object()) { continue; }
      snapshot.tasks.push_back(fromJsonTask(task.as_object()));
    }
  }
  return snapshot;
}

auto loadSnapshot(fs::path const& path) -> eh::Result<Snapshot> {
  auto input = std::ifstream{path};
  if (!input.is_open()) {
    return eh::makeError("Failed to open state file: {}", path.string());
  }

  auto content =
    std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};

  try {
    auto value = json::parse(content);
    if (!value.is_object()) {
      return eh::makeError("State file root must be a JSON object: {}", path.string());
    }
    return fromJsonSnapshot(value.as_object());
  } catch (std::exception const& ex) {
    return eh::makeError("Failed to parse state file {}: {}", path.string(), ex.what());
  }
}

auto fingerprintChanged(TaskRecord const& task) -> bool {
  return task.fingerprint != currentFingerprint(task);
}

void clearExecutionState(TaskRecord& task) {
  task.status = TaskStatus::Pending;
  task.lastProgress.reset();
  task.lastFrameCount.reset();
  task.lastStatus.reset();
  task.lastError.reset();
  task.startedAtMs.reset();
  task.updatedAtMs.reset();
  task.finishedAtMs.reset();
  task.segmentIndex.reset();
  task.resumeTimeUs.reset();
}

void markRestoredSucceeded(
  TaskRecord& task,
  std::string_view status = "restored from existing output"
) {
  auto const restoredAt = nowMs();
  task.status = TaskStatus::Succeeded;
  task.lastProgress = 100.0f;
  task.lastError.reset();
  task.lastStatus = std::string{status};
  if (!task.finishedAtMs.has_value()) { task.finishedAtMs = restoredAt; }
  task.updatedAtMs = restoredAt;
}

void normalizeExistingTask(TaskRecord& task) {
  auto const wasRunning = task.status == TaskStatus::Running;
  if (task.status == TaskStatus::Running) {
    task.status = TaskStatus::Interrupted;
    if (!task.lastError.has_value()) {
      task.lastError = "interrupted during previous run";
    }
  }

  if (fingerprintChanged(task)) {
    clearExecutionState(task);
    task.fingerprint = currentFingerprint(task);
    return;
  }

  if (task.segmentIndex.has_value()) {
    if (task.status == TaskStatus::Succeeded && !actionTargetExists(task)) {
      clearExecutionState(task);
    }
    return;
  }

  if (
    !wasRunning && actionTargetExists(task)
    && (task.status == TaskStatus::Pending
      || task.status == TaskStatus::Interrupted
      || task.status == TaskStatus::Succeeded)
  ) {
    markRestoredSucceeded(task);
    return;
  }

  if (task.status == TaskStatus::Succeeded && !actionTargetExists(task)) {
    clearExecutionState(task);
  }
}

auto buildFallbackStateFilePath(appctx::AppConfig const& config) -> fs::path {
  auto inputKeys = std::vector<std::string>{};
  if (!config.inputPaths.empty()) {
    inputKeys = toPathStrings(config.inputPaths);
  } else if (!config.inputPath.empty()) {
    inputKeys.push_back(config.inputPath.lexically_normal().generic_string());
  }
  std::ranges::sort(inputKeys);

  auto seed = std::format(
    "{}|{}|{}|{}",
    config.processType,
    config.outputFormat,
    outputLayoutToString(config.outputLayout),
    std::accumulate(
      inputKeys.begin(),
      inputKeys.end(),
      std::string{},
      [](std::string acc, std::string const& value) {
        if (!acc.empty()) { acc += '|'; }
        acc += value;
        return acc;
      }
    )
  );

  return fs::temp_directory_path()
    / "encro"
    / "jobs"
    / std::format("{}.job-state.json", collisionnaming::shortPathHash(seed));
}

auto commonParent(std::span<fs::path const> paths) -> std::optional<fs::path> {
  if (paths.empty()) { return std::nullopt; }
  auto root = paths.front().parent_path();
  for (auto const& path: paths) {
    if (path.parent_path() != root) { return std::nullopt; }
  }
  return root;
}

auto flushSnapshot(
  fs::path const& stateFilePath,
  Snapshot& snapshot,
  std::int64_t& lastFlushAtMs,
  bool force
) -> eh::Result<void> {
  auto const now = nowMs();
  if (!force && now - lastFlushAtMs < kFlushIntervalMs) { return {}; }

  snapshot.updatedAtMs = now;

  auto const tempPath = makeTempStatePath(stateFilePath);
  auto output = std::ofstream{tempPath, std::ios::trunc};
  if (!output) {
    return eh::makeError(
      "Failed to open state temp file for writing: {}",
      tempPath.string()
    );
  }
  output << json::serialize(toJson(snapshot));
  output.flush();
  if (!output) {
    return eh::makeError("Failed to write state snapshot to: {}", tempPath.string());
  }
  output.close();

  auto ec = std::error_code{};
  fs::rename(tempPath, stateFilePath, ec);
  if (ec) {
    fs::remove(stateFilePath, ec);
    ec.clear();
    fs::rename(tempPath, stateFilePath, ec);
  }
  if (ec) {
    return eh::makeError(
      "Failed to move state file into place: {} -> {}: {}",
      tempPath.string(),
      stateFilePath.string(),
      ec.message()
    );
  }

  lastFlushAtMs = now;
  return {};
}

}  // namespace detail

auto buildDefaultStateFilePath(appctx::AppConfig const& config) -> fs::path {
  if (config.stateFilePath.has_value()) { return config.stateFilePath.value(); }

  if (config.outputPath.has_value()) {
    return config.outputPath.value() / "encro.job-state.json";
  }

  if (!config.inputPaths.empty()) {
    if (auto const parent = detail::commonParent(config.inputPaths); parent.has_value()) {
      return parent.value() / "encro.job-state.json";
    }
    return detail::buildFallbackStateFilePath(config);
  }

  if (!config.inputPath.empty()) {
    auto const base = fs::is_directory(config.inputPath) ? config.inputPath
                                                         : config.inputPath.parent_path();
    return base / "encro.job-state.json";
  }

  return detail::buildFallbackStateFilePath(config);
}

auto buildConfigSnapshot(appctx::AppConfig const& config) -> ConfigSnapshot {
  auto inputPaths = config.inputPaths;
  if (inputPaths.empty() && !config.inputPath.empty()) {
    inputPaths.push_back(config.inputPath);
  }
  std::ranges::sort(inputPaths, [](fs::path const& lhs, fs::path const& rhs) {
    return collisionnaming::stablePathString(lhs)
      < collisionnaming::stablePathString(rhs);
  });

  return ConfigSnapshot{
    .processType = config.processType,
    .outputFormat = config.outputFormat,
    .outputLayout = detail::outputLayoutToString(config.outputLayout),
    .packOutput = config.packOutput,
    .packOnly = config.packOnly,
    .recursive = config.recursive,
    .forceNameConflictHandling = config.forceNameConflictHandling,
    .pictureFolderSummary = config.pictureFolderSummary,
    .inputPaths = std::move(inputPaths),
    .outputPath = config.outputPath,
  };
}

auto configMatches(ConfigSnapshot const& lhs, ConfigSnapshot const& rhs) -> bool {
  return lhs.processType == rhs.processType
    && lhs.outputFormat == rhs.outputFormat
    && lhs.outputLayout == rhs.outputLayout
    && (!lhs.packOutput || lhs.packOutput == rhs.packOutput)
    && lhs.packOnly == rhs.packOnly
    && lhs.recursive == rhs.recursive
    && lhs.forceNameConflictHandling == rhs.forceNameConflictHandling
    && lhs.pictureFolderSummary == rhs.pictureFolderSummary
    && lhs.inputPaths == rhs.inputPaths
    && lhs.outputPath == rhs.outputPath;
}

auto makeEncodeTask(fs::path const& inputPath, fs::path const& plannedOutputFile)
  -> TaskRecord {
  auto const sourcePaths = std::vector<fs::path>{inputPath};
  auto const targetPaths = std::vector<fs::path>{plannedOutputFile};

  return TaskRecord{
    .id = std::format("encode:{}", collisionnaming::stablePathString(inputPath)),
    .kind = std::string{kEncodeVideoKind},
    .status = TaskStatus::Pending,
    .label = inputPath.filename().string(),
    .attemptCount = 0,
    .fingerprint = detail::buildFingerprint(kEncodeVideoKind, sourcePaths, targetPaths),
    .sourcePaths = sourcePaths,
    .targetPaths = targetPaths,
    .lastProgress = std::nullopt,
    .lastFrameCount = std::nullopt,
    .lastStatus = std::nullopt,
    .lastError = std::nullopt,
    .startedAtMs = std::nullopt,
    .updatedAtMs = detail::nowMs(),
    .finishedAtMs = std::nullopt,
  };
}

auto makeArchiveTask(
  fs::path const& archiveFile,
  std::span<fs::path const> members,
  std::string label
) -> TaskRecord {
  auto sourcePaths = std::vector<fs::path>{members.begin(), members.end()};
  auto const targetPaths = std::vector<fs::path>{archiveFile};

  return TaskRecord{
    .id = std::format("archive:{}", collisionnaming::stablePathString(archiveFile)),
    .kind = std::string{kBuildArchiveKind},
    .status = TaskStatus::Pending,
    .label = std::move(label),
    .attemptCount = 0,
    .fingerprint = detail::buildFingerprint(kBuildArchiveKind, sourcePaths, targetPaths),
    .sourcePaths = std::move(sourcePaths),
    .targetPaths = targetPaths,
    .lastProgress = std::nullopt,
    .lastFrameCount = std::nullopt,
    .lastStatus = std::nullopt,
    .lastError = std::nullopt,
    .startedAtMs = std::nullopt,
    .updatedAtMs = detail::nowMs(),
    .finishedAtMs = std::nullopt,
  };
}

auto makeCompressPhaseTask() -> TaskRecord {
  return TaskRecord{
    .id = std::string{kCompressPhaseTaskId},
    .kind = std::string{kCompressPhaseKind},
    .label = "compress pictures",
    .attemptCount = 0,
    .fingerprint = detail::buildFingerprint(kCompressPhaseKind, {}, {}),
    .updatedAtMs = detail::nowMs(),
  };
}

auto primarySourcePath(TaskRecord const& task) -> std::optional<fs::path> {
  if (task.sourcePaths.empty()) { return std::nullopt; }
  return task.sourcePaths.front();
}

auto primaryTargetPath(TaskRecord const& task) -> std::optional<fs::path> {
  if (task.targetPaths.empty()) { return std::nullopt; }
  return task.targetPaths.front();
}

auto needsExecution(TaskRecord const& task) -> bool {
  return task.status != TaskStatus::Succeeded;
}

auto actionTargetExists(TaskRecord const& task) -> bool {
  auto ec = std::error_code{};
  if (task.targetPaths.empty()) { return false; }

  return std::ranges::all_of(task.targetPaths, [&](fs::path const& path) {
    return fs::exists(path, ec) && !ec;
  });
}

}  // namespace jobstate
