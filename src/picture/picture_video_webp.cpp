#include "picture/picture_video_webp.h"

#include "core/job_state.h"
#include "core/task_executor.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "video/video_encode_runner.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>

#include "logging/log_tags.h"
#include "logging/logging.h"

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::PICTURE_PROCESS);

namespace fs = std::filesystem;
using enum terminal::MessageKind;

namespace {

// Each conversion is a single-core libwebp encode plus an ffmpeg decode, so a
// picture-style size-derived cap would oversubscribe the machine while gaining
// little; throughput is bounded per conversion, not per machine.
constexpr auto kVideoConversionMaxParallel = std::size_t{2};

// The encoder writes here and the phase renames on success, so a file at the
// final cached path is always a complete output. Mirrors the picture
// compression temp naming (recognizable media extension, `.partial` marker).
auto conversionTempPath(fs::path const& outputPath) -> fs::path {
  return outputPath.parent_path()
    / (outputPath.stem().string() + ".partial" + outputPath.extension().string());
}

auto conversionActionId(picturewebp::ConversionTask const& task) -> std::string {
  return jobstate::makeEncodeTask(task.sourcePath, task.outputPath).id;
}

// A cached output may be reused only when the saved state already records a
// succeeded conversion of this exact source. Anything else - a missing record,
// one reset because its source changed, an interrupted or failed one - drops
// the file, because the merge restores any Pending/Interrupted task whose
// target exists. The purge must run before the merge, not after it.
bool cacheBackedByState(jobstate::Store* store, jobstate::TaskRecord const& planned) {
  if (store == nullptr) { return false; }
  auto const saved = store->findTask(planned.id);
  return saved.has_value()
    && saved->status == jobstate::TaskStatus::Succeeded
    && saved->fingerprint == planned.fingerprint;
}

void dropCachedOutput(fs::path const& outputPath) {
  auto ec = std::error_code{};
  fs::remove(outputPath, ec);
  fs::remove(conversionTempPath(outputPath), ec);
}

// Renames the finished encode onto its final cached name. A failure here is a
// failed conversion: the temp file is never packed.
bool finalizeConvertedOutput(fs::path const& outputPath) {
  auto const tempPath = conversionTempPath(outputPath);
  if (!fs::exists(tempPath)) { return false; }

  auto ec = std::error_code{};
  fs::remove(outputPath, ec);
  fs::rename(tempPath, outputPath, ec);
  if (ec) {
    LOG_WARN(
      "Video conversion output rename failed: output={} error={}",
      outputPath.string(),
      ec.message()
    );
    return false;
  }
  return true;
}

struct ConversionBatchState {
  appctx::AppContext* ctx = nullptr;
  jobstate::Store* store = nullptr;
  std::size_t total = 0;
  std::size_t barIndex = 0;
  std::atomic_size_t completed{0};
  progress::ProgressContext* progressCtx = nullptr;
  std::mutex mtx;
  std::vector<picturewebp::ConversionTask> converted;
  std::map<fs::path, std::string> failureReasons;
};

void recordConversionOutcome(
  ConversionBatchState& state,
  picturewebp::ConversionTask const& task,
  std::string_view actionId,
  std::optional<std::string> failureReason
) {
  auto lock = std::scoped_lock{state.mtx};
  if (failureReason.has_value()) {
    auto const reason =
      failureReason->empty() ? std::string{"conversion failed"} : failureReason.value();
    state.failureReasons.emplace(task.sourcePath, reason);
    if (state.store != nullptr) { state.store->markFailed(actionId, reason); }
    return;
  }

  state.converted.push_back(task);
  if (state.store != nullptr) { state.store->markSucceeded(actionId); }
}

auto runConversionTask(
  ConversionBatchState& state,
  picturewebp::ConversionTask const& task
) -> eh::Result<void> {
  if (stopsignal::isStopRequested()) {
    return eh::makeError("Video conversion canceled by user.");
  }

  auto const actionId = conversionActionId(task);
  if (state.store != nullptr) { state.store->markRunning(actionId); }

  auto encodingState = appctx::EncodingState{};
  encodingState.inputPath = task.sourcePath;
  encodingState.actionId = actionId;
  // The encoder writes the temp path; the final cached name appears only after
  // the encoder exited successfully.
  encodingState.plannedOutputFile = conversionTempPath(task.outputPath);

  auto const statusUpdater = [&state](std::string const& status) {
    state.progressCtx->setPostfixText(
      state.barIndex,
      std::format(
        "Converting videos: {}/{} [{}]",
        state.completed.load(std::memory_order_acquire),
        state.total,
        status
      )
    );
  };

  auto const encoded = encodeVideo(*state.ctx, encodingState, "webp", statusUpdater);
  auto const completed = encoded && finalizeConvertedOutput(task.outputPath);
  std::optional<std::string> failureReason;
  if (!completed) {
    auto const lock =
      std::scoped_lock{encodingState.mtx};  // NOLINT(bugprone-unused-raii)
    failureReason = encodingState.lastError.value_or(std::string{});
  }
  recordConversionOutcome(state, task, actionId, failureReason);

  auto const done = state.completed.fetch_add(1, std::memory_order_release) + 1;
  state.progressCtx->setProgress(
    state.barIndex,
    static_cast<float>(done) / static_cast<float>(state.total) * 100.0f
  );
  state.progressCtx->setPostfixText(
    state.barIndex,
    std::format("Converting videos: {}/{}", done, state.total)
  );

  if (!completed) {
    return eh::makeError("Failed to convert {}", task.sourcePath.string());
  }
  return {};
}

// Splits the run's conversions into what the saved state already backs and what
// has to be encoded, dropping the cache entries the state does not back.
auto planPendingConversions(
  jobstate::Store* store,
  std::span<picturewebp::ConversionTask const> tasks
)
  -> std::pair<
    std::vector<picturewebp::ConversionTask>,
    std::vector<picturewebp::ConversionTask>
  > {
  auto plannedRecords = std::vector<jobstate::TaskRecord>{};
  plannedRecords.reserve(tasks.size());
  for (auto const& task: tasks) {
    plannedRecords.push_back(jobstate::makeEncodeTask(task.sourcePath, task.outputPath));
  }

  for (auto index = std::size_t{0}; index < tasks.size(); ++index) {
    if (cacheBackedByState(store, plannedRecords[index])) { continue; }
    dropCachedOutput(tasks[index].outputPath);
  }

  auto pending = std::vector<picturewebp::ConversionTask>{};
  auto ready = std::vector<picturewebp::ConversionTask>{};
  if (store == nullptr) {
    pending.assign(tasks.begin(), tasks.end());
    return {std::move(pending), std::move(ready)};
  }

  for (auto const& task: store->mergeTasks(plannedRecords)) {
    auto const sourcePath = jobstate::primarySourcePath(task);
    if (!sourcePath.has_value()) { continue; }
    auto const found =
      std::ranges::find_if(tasks, [&](picturewebp::ConversionTask const& candidate) {
        return candidate.sourcePath == sourcePath.value();
      });
    if (found == tasks.end()) { continue; }
    if (jobstate::needsExecution(task)) {
      pending.push_back(*found);
    } else {
      ready.push_back(*found);
    }
  }

  return {std::move(pending), std::move(ready)};
}

}  // namespace

auto picturewebp::runConversionPhase(
  appctx::AppContext& ctx,
  std::span<ConversionTask const> tasks,
  std::size_t maxParallel
) -> eh::Result<ConversionOutcome> {
  if (tasks.empty()) { return ConversionOutcome{}; }

  auto* store = ctx.runtime.jobState.get();
  auto [pending, ready] = planPendingConversions(store, tasks);

  if (pending.empty()) {
    terminal::println(
      Info,
      "Recovered {} converted video(s) from the conversion cache.",
      terminal::count(ready.size())
    );
    return ConversionOutcome{.canceled = false, .ready = std::move(ready)};
  }

  terminal::println(
    Info,
    "Converting {} video(s) to WebP...",
    terminal::count(pending.size())
  );

  auto progressCtx = progress::ProgressContext{};
  auto state = ConversionBatchState{
    .ctx = &ctx,
    .store = store,
    .total = pending.size(),
    .progressCtx = &progressCtx,
  };
  state.barIndex = progressCtx.addBar(
    std::format("Converting videos: 0/{}", pending.size()),
    terminal::Role::Accent
  );

  auto taskSpecs = std::vector<taskexec::TaskSpec>{};
  taskSpecs.reserve(pending.size());
  for (auto const& task: pending) {
    taskSpecs.push_back({
      .id = std::format("convert:{}", task.outputPath.string()),
      .label = task.sourcePath.filename().string(),
      .input = task.sourcePath.string(),
      .run = [&state, &task](taskexec::TaskContext&) {
        return runConversionTask(state, task);
      },
    });
  }

  auto const runState = taskexec::runTasks({
    .tasks = std::move(taskSpecs),
    .maxConcurrency =
      std::max(std::size_t{1}, std::min(maxParallel, kVideoConversionMaxParallel)),
    .progress = &progressCtx,
    .hideCursor = true,
  });

  if (runState.canceled || stopsignal::isStopRequested()) {
    auto pendingIds = std::vector<std::string>{};
    pendingIds.reserve(pending.size());
    for (auto const& task: pending) { pendingIds.push_back(conversionActionId(task)); }
    if (store != nullptr) {
      store->markIncompleteInterrupted(pendingIds, "canceled by user");
    }

    auto const converted = [&]() {
      auto const lock = std::scoped_lock{state.mtx};  // NOLINT(bugprone-unused-raii)
      return state.converted.size();
    }();
    progressCtx.setRole(state.barIndex, terminal::Role::Bad);
    progressCtx.setPostfixText(
      state.barIndex,
      std::format("Canceled: {}/{}", converted, pending.size())
    );
    terminal::messageln(Warning, "Video conversion canceled by user.");
    return ConversionOutcome{.canceled = true, .ready = std::move(ready)};
  }

  auto const collected = [&]() {
    auto const lock = std::scoped_lock{state.mtx};  // NOLINT(bugprone-unused-raii)
    for (auto const& [path, reason]: state.failureReasons) {
      terminal::println(Plain, "  {}: {}", terminal::path(path), reason);
    }
    return std::pair{state.failureReasons.size(), state.converted};
  }();
  for (auto const& task: collected.second) { ready.push_back(task); }

  progressCtx.setRole(state.barIndex, terminal::Role::Good);
  progressCtx.setPostfixText(
    state.barIndex,
    std::format(
      "Converted: {}/{}{}",
      ready.size(),
      tasks.size(),
      collected.first > 0 ? " (some failed)" : ""
    )
  );

  if (ready.empty()) { return eh::makeError("All video conversions failed."); }

  return ConversionOutcome{.canceled = false, .ready = std::move(ready)};
}
