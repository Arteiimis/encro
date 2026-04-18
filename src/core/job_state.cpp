#include "core/job_state.h"

#include "core/collision_naming.h"
#include "utils/utils.h"

#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <format>
#include <numeric>
#include <utility>

namespace jobstate {

namespace json = boost::json;
using namespace std::chrono;

namespace {

constexpr auto kStateVersion = 1;
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
  return static_cast<std::int64_t>(
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

auto actionKindToString(ActionKind kind) -> std::string_view {
  switch (kind) {
    case ActionKind::EncodeVideo : return "encode_video";
    case ActionKind::BuildArchive: return "build_archive";
  }

  return "encode_video";
}

auto parseActionKind(std::string_view value) -> ActionKind {
  if (value == "build_archive") { return ActionKind::BuildArchive; }
  return ActionKind::EncodeVideo;
}

auto actionStatusToString(ActionStatus status) -> std::string_view {
  switch (status) {
    case ActionStatus::Pending    : return "pending";
    case ActionStatus::Running    : return "running";
    case ActionStatus::Succeeded  : return "succeeded";
    case ActionStatus::Failed     : return "failed";
    case ActionStatus::Interrupted: return "interrupted";
  }

  return "pending";
}

auto parseActionStatus(std::string_view value) -> ActionStatus {
  if (value == "running") { return ActionStatus::Running; }
  if (value == "succeeded") { return ActionStatus::Succeeded; }
  if (value == "failed") { return ActionStatus::Failed; }
  if (value == "interrupted") { return ActionStatus::Interrupted; }
  return ActionStatus::Pending;
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

auto toJson(ConfigSnapshot const& config) -> json::object {
  auto object = json::object{};
  object["processType"] = config.processType;
  object["outputFormat"] = config.outputFormat;
  object["outputLayout"] = config.outputLayout;
  object["packOutput"] = config.packOutput;
  object["packOnly"] = config.packOnly;
  object["recursive"] = config.recursive;
  object["forceNameConflictHandling"] = config.forceNameConflictHandling;
  object["inputPaths"] = pathsToJson(config.inputPaths);
  object["outputPath"] = pathToJson(config.outputPath);
  return object;
}

auto fromJsonConfig(json::object const& object) -> ConfigSnapshot {
  return ConfigSnapshot{
    .processType = optionalStringFrom(object, "processType").value_or("video"),
    .outputFormat = optionalStringFrom(object, "outputFormat").value_or("mp4"),
    .outputLayout = optionalStringFrom(object, "outputLayout").value_or("flat"),
    .packOutput =
      object.if_contains("packOutput") && object.at("packOutput").as_bool(),
    .packOnly = object.if_contains("packOnly") && object.at("packOnly").as_bool(),
    .recursive = object.if_contains("recursive") && object.at("recursive").as_bool(),
    .forceNameConflictHandling = !object.if_contains("forceNameConflictHandling")
      || object.at("forceNameConflictHandling").as_bool(),
    .inputPaths = optionalStringArrayFrom(object, "inputPaths"),
    .outputPath = optionalPathFrom(object, "outputPath"),
  };
}

auto toJson(ActionRecord const& action) -> json::object {
  auto object = json::object{};
  object["id"] = action.id;
  object["kind"] = actionKindToString(action.kind);
  object["status"] = actionStatusToString(action.status);
  object["label"] = action.label;
  object["attemptCount"] = action.attemptCount;
  object["inputPath"] = pathToJson(action.inputPath);
  object["plannedOutputFile"] = pathToJson(action.plannedOutputFile);
  object["inputSize"] = action.inputSize.has_value() ? json::value(*action.inputSize)
                                                     : json::value(nullptr);
  object["inputWriteTime"] = action.inputWriteTime.has_value()
    ? json::value(*action.inputWriteTime)
    : json::value(nullptr);
  object["archiveFile"] = pathToJson(action.archiveFile);
  if (!action.archiveMembers.empty()) {
    object["archiveMembers"] = pathsToJson(action.archiveMembers);
  }
  object["lastProgress"] = action.lastProgress.has_value()
    ? json::value(*action.lastProgress)
    : json::value(nullptr);
  object["lastFrameCount"] = action.lastFrameCount.has_value()
    ? json::value(*action.lastFrameCount)
    : json::value(nullptr);
  object["lastStatus"] = action.lastStatus.has_value()
    ? json::value(*action.lastStatus)
    : json::value(nullptr);
  object["lastError"] = action.lastError.has_value() ? json::value(*action.lastError)
                                                     : json::value(nullptr);
  object["startedAtMs"] = action.startedAtMs.has_value()
    ? json::value(*action.startedAtMs)
    : json::value(nullptr);
  object["updatedAtMs"] = action.updatedAtMs.has_value()
    ? json::value(*action.updatedAtMs)
    : json::value(nullptr);
  object["finishedAtMs"] = action.finishedAtMs.has_value()
    ? json::value(*action.finishedAtMs)
    : json::value(nullptr);
  return object;
}

auto fromJsonAction(json::object const& object) -> ActionRecord {
  return ActionRecord{
    .id = optionalStringFrom(object, "id").value_or({}),
    .kind =
      parseActionKind(optionalStringFrom(object, "kind").value_or("encode_video")),
    .status =
      parseActionStatus(optionalStringFrom(object, "status").value_or("pending")),
    .label = optionalStringFrom(object, "label").value_or({}),
    .attemptCount =
      optionalNumberFrom<std::size_t>(object, "attemptCount").value_or(0),
    .inputPath = optionalPathFrom(object, "inputPath"),
    .plannedOutputFile = optionalPathFrom(object, "plannedOutputFile"),
    .inputSize = optionalNumberFrom<std::uintmax_t>(object, "inputSize"),
    .inputWriteTime = optionalNumberFrom<std::int64_t>(object, "inputWriteTime"),
    .archiveFile = optionalPathFrom(object, "archiveFile"),
    .archiveMembers = optionalStringArrayFrom(object, "archiveMembers"),
    .lastProgress = optionalNumberFrom<float>(object, "lastProgress"),
    .lastFrameCount = optionalNumberFrom<std::uint64_t>(object, "lastFrameCount"),
    .lastStatus = optionalStringFrom(object, "lastStatus"),
    .lastError = optionalStringFrom(object, "lastError"),
    .startedAtMs = optionalNumberFrom<std::int64_t>(object, "startedAtMs"),
    .updatedAtMs = optionalNumberFrom<std::int64_t>(object, "updatedAtMs"),
    .finishedAtMs = optionalNumberFrom<std::int64_t>(object, "finishedAtMs"),
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

  auto actions = json::array{};
  actions.reserve(snapshot.actions.size());
  for (auto const& action: snapshot.actions) {
    actions.emplace_back(toJson(action));
  }
  object["actions"] = std::move(actions);
  return object;
}

auto fromJsonSnapshot(json::object const& object) -> Snapshot {
  auto snapshot = Snapshot{};
  snapshot.version =
    optionalNumberFrom<int>(object, "version").value_or(kStateVersion);
  snapshot.jobId = optionalStringFrom(object, "jobId").value_or({});
  snapshot.stage = optionalStringFrom(object, "stage").value_or("planning");
  snapshot.cancelRequested =
    object.if_contains("cancelRequested") && object.at("cancelRequested").as_bool();
  snapshot.updatedAtMs =
    optionalNumberFrom<std::int64_t>(object, "updatedAtMs").value_or(0);
  if (object.if_contains("config") && object.at("config").is_object()) {
    snapshot.config = fromJsonConfig(object.at("config").as_object());
  }
  if (object.if_contains("actions") && object.at("actions").is_array()) {
    for (auto const& action: object.at("actions").as_array()) {
      if (!action.is_object()) { continue; }
      snapshot.actions.push_back(fromJsonAction(action.as_object()));
    }
  }
  return snapshot;
}

auto loadSnapshot(fs::path const& path) -> eh::Result<Snapshot> {
  auto input = std::ifstream{path};
  if (!input.is_open()) {
    return eh::makeError("Failed to open state file: {}", path.string());
  }

  auto content = std::string{
    std::istreambuf_iterator<char>{input},
    std::istreambuf_iterator<char>{}
  };

  try {
    auto value = json::parse(content);
    if (!value.is_object()) {
      return eh::makeError(
        "State file root must be a JSON object: {}",
        path.string()
      );
    }
    return fromJsonSnapshot(value.as_object());
  } catch (std::exception const& ex) {
    return eh::makeError(
      "Failed to parse state file {}: {}",
      path.string(),
      ex.what()
    );
  }
}

auto samePlan(ActionRecord const& lhs, ActionRecord const& rhs) -> bool {
  return lhs.kind == rhs.kind
    && lhs.inputPath == rhs.inputPath
    && lhs.plannedOutputFile == rhs.plannedOutputFile
    && lhs.archiveFile == rhs.archiveFile
    && lhs.archiveMembers == rhs.archiveMembers
    && lhs.inputSize == rhs.inputSize
    && lhs.inputWriteTime == rhs.inputWriteTime;
}

auto inputChanged(ActionRecord const& action) -> bool {
  if (!action.inputPath.has_value()) { return false; }

  auto const currentSize = inputSize(action.inputPath.value());
  auto const currentWriteTime = inputWriteTime(action.inputPath.value());
  if (!currentSize.has_value() || !currentWriteTime.has_value()) { return true; }

  return action.inputSize != currentSize
    || action.inputWriteTime != currentWriteTime;
}

void clearExecutionState(ActionRecord& action) {
  action.status = ActionStatus::Pending;
  action.lastProgress.reset();
  action.lastFrameCount.reset();
  action.lastStatus.reset();
  action.lastError.reset();
  action.startedAtMs.reset();
  action.updatedAtMs.reset();
  action.finishedAtMs.reset();
}

void markRestoredSucceeded(
  ActionRecord& action,
  std::string_view status = "restored from existing output"
) {
  auto const restoredAt = nowMs();
  action.status = ActionStatus::Succeeded;
  action.lastProgress = 100.0f;
  action.lastError.reset();
  action.lastStatus = std::string{status};
  if (!action.finishedAtMs.has_value()) { action.finishedAtMs = restoredAt; }
  action.updatedAtMs = restoredAt;
}

void normalizeExistingAction(ActionRecord& action) {
  auto const wasRunning = action.status == ActionStatus::Running;
  if (action.status == ActionStatus::Running) {
    action.status = ActionStatus::Interrupted;
    if (!action.lastError.has_value()) {
      action.lastError = "interrupted during previous run";
    }
  }

  if (inputChanged(action)) {
    clearExecutionState(action);
    return;
  }

  if (
    !wasRunning && actionTargetExists(action)
    && (action.status == ActionStatus::Pending
      || action.status == ActionStatus::Interrupted
      || action.status == ActionStatus::Succeeded)
  ) {
    markRestoredSucceeded(action);
    return;
  }

  if (action.status == ActionStatus::Succeeded && !actionTargetExists(action)) {
    clearExecutionState(action);
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

}  // namespace

Store::Store(fs::path stateFilePath): stateFilePath_(std::move(stateFilePath)) { }

auto Store::stateFilePath() const -> fs::path const& {
  return stateFilePath_;
}

auto Store::initialize(appctx::AppConfig const& config, bool restart)
  -> eh::Result<bool> {
  auto lock = std::scoped_lock{mtx_};
  auto ec = std::error_code{};
  fs::create_directories(stateFilePath_.parent_path(), ec);

  auto const currentConfig = buildConfigSnapshot(config);
  auto const stateExists = fs::exists(stateFilePath_, ec) && !ec;
  if (!restart && !stateExists && config.resumeState) {
    return eh::makeError(
      "Resume requested but no state file was found: {}",
      stateFilePath_.string()
    );
  }

  if (!restart && stateExists) {
    auto const loaded = loadSnapshot(stateFilePath_);
    if (!loaded) { return eh::makeError("{}", loaded.error()); }

    if (!configMatches(loaded->config, currentConfig)) {
      if (config.resumeState) {
        return eh::makeError(
          "State file does not match current command: {}",
          stateFilePath_.string()
        );
      }
    } else {
      snapshot_ = loaded.value();
      snapshot_.config = currentConfig;
      snapshot_.cancelRequested = false;
      snapshot_.updatedAtMs = nowMs();
      for (auto& action: snapshot_.actions) { normalizeExistingAction(action); }
      rebuildIndexLocked();
      flushLocked(true);
      return true;
    }
  }

  snapshot_ = Snapshot{
    .version = kStateVersion,
    .jobId = getUUID(),
    .stage = "planning",
    .cancelRequested = false,
    .updatedAtMs = nowMs(),
    .config = currentConfig,
    .actions = {},
  };
  rebuildIndexLocked();
  flushLocked(true);
  return false;
}

auto Store::mergeActions(std::span<ActionRecord const> plannedActions)
  -> std::vector<ActionRecord> {
  auto lock = std::scoped_lock{mtx_};
  auto mergedActions = std::vector<ActionRecord>{};
  mergedActions.reserve(plannedActions.size());

  for (auto const& plannedAction: plannedActions) {
    if (auto const index = indexFor(plannedAction.id); index.has_value()) {
      auto& existing = snapshot_.actions[index.value()];

      auto preserved = existing;
      preserved.kind = plannedAction.kind;
      preserved.label = plannedAction.label;
      preserved.inputPath = plannedAction.inputPath;
      preserved.plannedOutputFile = plannedAction.plannedOutputFile;
      preserved.inputSize = plannedAction.inputSize;
      preserved.inputWriteTime = plannedAction.inputWriteTime;
      preserved.archiveFile = plannedAction.archiveFile;
      preserved.archiveMembers = plannedAction.archiveMembers;

      normalizeExistingAction(preserved);

      existing = std::move(preserved);
      mergedActions.push_back(existing);
      continue;
    }

    auto action = plannedAction;
    action.updatedAtMs = nowMs();
    snapshot_.actions.push_back(std::move(action));
    actionIndex_[snapshot_.actions.back().id] = snapshot_.actions.size() - 1;
    mergedActions.push_back(snapshot_.actions.back());
  }

  snapshot_.updatedAtMs = nowMs();
  flushLocked(true);
  return mergedActions;
}

auto Store::actions() const -> std::vector<ActionRecord> {
  auto lock = std::scoped_lock{mtx_};
  return snapshot_.actions;
}

auto Store::findAction(std::string_view id) const -> std::optional<ActionRecord> {
  auto lock = std::scoped_lock{mtx_};
  auto const index = indexFor(id);
  if (!index.has_value()) { return std::nullopt; }
  return snapshot_.actions[index.value()];
}

void Store::setStage(std::string_view stage) {
  auto lock = std::scoped_lock{mtx_};
  snapshot_.stage = std::string{stage};
  snapshot_.updatedAtMs = nowMs();
  flushLocked(true);
}

void Store::requestCancel() {
  auto lock = std::scoped_lock{mtx_};
  snapshot_.cancelRequested = true;
  snapshot_.stage = "canceling";
  snapshot_.updatedAtMs = nowMs();
  flushLocked(true);
}

auto Store::isCancelRequested() const -> bool {
  auto lock = std::scoped_lock{mtx_};
  return snapshot_.cancelRequested;
}

void Store::markRunning(std::string_view id) {
  auto lock = std::scoped_lock{mtx_};
  auto const index = indexFor(id);
  if (!index.has_value()) { return; }

  auto& action = snapshot_.actions[index.value()];
  action.status = ActionStatus::Running;
  action.attemptCount += 1;
  action.startedAtMs = nowMs();
  action.updatedAtMs = action.startedAtMs;
  action.finishedAtMs.reset();
  action.lastError.reset();
  flushLocked(true);
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

  auto& action = snapshot_.actions[index.value()];
  if (progress.has_value()) { action.lastProgress = progress.value(); }
  if (frameCount.has_value()) { action.lastFrameCount = frameCount.value(); }
  if (status.has_value()) { action.lastStatus = std::string{status.value()}; }
  action.updatedAtMs = nowMs();
  snapshot_.updatedAtMs = action.updatedAtMs.value();
  flushLocked(false);
}

void Store::markSucceeded(
  std::string_view id,
  std::optional<std::string_view> status
) {
  auto lock = std::scoped_lock{mtx_};
  auto const index = indexFor(id);
  if (!index.has_value()) { return; }

  auto& action = snapshot_.actions[index.value()];
  action.status = ActionStatus::Succeeded;
  action.lastProgress = 100.0f;
  if (status.has_value()) { action.lastStatus = std::string{status.value()}; }
  action.lastError.reset();
  action.finishedAtMs = nowMs();
  action.updatedAtMs = action.finishedAtMs;
  snapshot_.updatedAtMs = action.finishedAtMs.value();
  flushLocked(true);
}

void Store::markFailed(std::string_view id, std::string_view error) {
  auto lock = std::scoped_lock{mtx_};
  auto const index = indexFor(id);
  if (!index.has_value()) { return; }

  auto& action = snapshot_.actions[index.value()];
  action.status = ActionStatus::Failed;
  action.lastError = std::string{error};
  action.finishedAtMs = nowMs();
  action.updatedAtMs = action.finishedAtMs;
  snapshot_.updatedAtMs = action.finishedAtMs.value();
  flushLocked(true);
}

void Store::markInterrupted(std::string_view id, std::string_view reason) {
  auto lock = std::scoped_lock{mtx_};
  auto const index = indexFor(id);
  if (!index.has_value()) { return; }

  auto& action = snapshot_.actions[index.value()];
  if (
    action.status == ActionStatus::Succeeded || action.status == ActionStatus::Failed
  ) {
    return;
  }

  action.status = ActionStatus::Interrupted;
  if (!reason.empty()) { action.lastError = std::string{reason}; }
  action.finishedAtMs = nowMs();
  action.updatedAtMs = action.finishedAtMs;
  snapshot_.updatedAtMs = action.finishedAtMs.value();
  flushLocked(true);
}

void Store::markIncompleteInterrupted(
  std::span<std::string const> ids,
  std::string_view reason
) {
  auto lock = std::scoped_lock{mtx_};
  auto changed = false;
  auto const now = nowMs();
  for (auto const& id: ids) {
    auto const index = indexFor(id);
    if (!index.has_value()) { continue; }

    auto& action = snapshot_.actions[index.value()];
    if (
      action.status == ActionStatus::Pending
      || action.status == ActionStatus::Running
    ) {
      action.status = ActionStatus::Interrupted;
      action.lastError = std::string{reason};
      action.finishedAtMs = now;
      action.updatedAtMs = now;
      changed = true;
    }
  }

  if (!changed) { return; }

  snapshot_.updatedAtMs = now;
  snapshot_.stage = "canceled";
  snapshot_.cancelRequested = true;
  flushLocked(true);
}

void Store::flush() {
  auto lock = std::scoped_lock{mtx_};
  flushLocked(true);
}

auto Store::indexFor(std::string_view id) const -> std::optional<std::size_t> {
  if (auto const it = actionIndex_.find(std::string{id}); it != actionIndex_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void Store::rebuildIndexLocked() {
  actionIndex_.clear();
  for (auto index = std::size_t{0}; index < snapshot_.actions.size(); ++index) {
    actionIndex_[snapshot_.actions[index].id] = index;
  }
}

void Store::flushLocked(bool force) {
  auto const now = nowMs();
  if (!force && now - lastFlushAtMs_ < kFlushIntervalMs) { return; }

  snapshot_.updatedAtMs = now;

  auto const tempPath = makeTempStatePath(stateFilePath_);
  auto output = std::ofstream{tempPath, std::ios::trunc};
  output << json::serialize(toJson(snapshot_));
  output.flush();
  output.close();

  auto ec = std::error_code{};
  fs::rename(tempPath, stateFilePath_, ec);
  if (ec) {
    fs::remove(stateFilePath_, ec);
    fs::rename(tempPath, stateFilePath_, ec);
  }

  lastFlushAtMs_ = now;
}

auto buildDefaultStateFilePath(appctx::AppConfig const& config) -> fs::path {
  if (config.stateFilePath.has_value()) { return config.stateFilePath.value(); }

  if (config.outputPath.has_value()) {
    return config.outputPath.value() / "encro.job-state.json";
  }

  if (!config.inputPaths.empty()) {
    if (auto const parent = commonParent(config.inputPaths); parent.has_value()) {
      return parent.value() / "encro.job-state.json";
    }
    return buildFallbackStateFilePath(config);
  }

  if (!config.inputPath.empty()) {
    auto const base = fs::is_directory(config.inputPath)
      ? config.inputPath
      : config.inputPath.parent_path();
    return base / "encro.job-state.json";
  }

  return buildFallbackStateFilePath(config);
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
    .outputLayout = outputLayoutToString(config.outputLayout),
    .packOutput = config.packOutput,
    .packOnly = config.packOnly,
    .recursive = config.recursive,
    .forceNameConflictHandling = config.forceNameConflictHandling,
    .inputPaths = std::move(inputPaths),
    .outputPath = config.outputPath,
  };
}

auto configMatches(ConfigSnapshot const& lhs, ConfigSnapshot const& rhs) -> bool {
  return lhs.processType == rhs.processType
    && lhs.outputFormat == rhs.outputFormat
    && lhs.outputLayout == rhs.outputLayout
    && lhs.packOutput == rhs.packOutput
    && lhs.packOnly == rhs.packOnly
    && lhs.recursive == rhs.recursive
    && lhs.forceNameConflictHandling == rhs.forceNameConflictHandling
    && lhs.inputPaths == rhs.inputPaths
    && lhs.outputPath == rhs.outputPath;
}

auto makeEncodeAction(fs::path const& inputPath, fs::path const& plannedOutputFile)
  -> ActionRecord {
  return ActionRecord{
    .id = std::format("encode:{}", collisionnaming::stablePathString(inputPath)),
    .kind = ActionKind::EncodeVideo,
    .status = ActionStatus::Pending,
    .label = inputPath.filename().string(),
    .attemptCount = 0,
    .inputPath = inputPath,
    .plannedOutputFile = plannedOutputFile,
    .inputSize = inputSize(inputPath),
    .inputWriteTime = inputWriteTime(inputPath),
    .archiveFile = std::nullopt,
    .archiveMembers = {},
    .lastProgress = std::nullopt,
    .lastFrameCount = std::nullopt,
    .lastStatus = std::nullopt,
    .lastError = std::nullopt,
    .startedAtMs = std::nullopt,
    .updatedAtMs = nowMs(),
    .finishedAtMs = std::nullopt,
  };
}

auto makeArchiveAction(
  fs::path const& archiveFile,
  std::span<fs::path const> members,
  std::string label
) -> ActionRecord {
  (void)members;

  return ActionRecord{
    .id = std::format("archive:{}", collisionnaming::stablePathString(archiveFile)),
    .kind = ActionKind::BuildArchive,
    .status = ActionStatus::Pending,
    .label = std::move(label),
    .attemptCount = 0,
    .inputPath = std::nullopt,
    .plannedOutputFile = std::nullopt,
    .inputSize = std::nullopt,
    .inputWriteTime = std::nullopt,
    .archiveFile = archiveFile,
    .archiveMembers = {},
    .lastProgress = std::nullopt,
    .lastFrameCount = std::nullopt,
    .lastStatus = std::nullopt,
    .lastError = std::nullopt,
    .startedAtMs = std::nullopt,
    .updatedAtMs = nowMs(),
    .finishedAtMs = std::nullopt,
  };
}

auto needsExecution(ActionRecord const& action) -> bool {
  return action.status != ActionStatus::Succeeded;
}

auto actionTargetExists(ActionRecord const& action) -> bool {
  auto ec = std::error_code{};
  if (
    action.kind == ActionKind::EncodeVideo && action.plannedOutputFile.has_value()
  ) {
    return fs::exists(action.plannedOutputFile.value(), ec) && !ec;
  }
  if (action.kind == ActionKind::BuildArchive && action.archiveFile.has_value()) {
    return fs::exists(action.archiveFile.value(), ec) && !ec;
  }
  return false;
}

}  // namespace jobstate
