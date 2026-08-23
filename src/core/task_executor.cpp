#include "core/task_executor.h"

#include "core/parallel.h"
#include "infra/stop_signal.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <format>
#include <optional>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::CORE_TASK);

namespace taskexec {

namespace {

auto makeTaskError(std::string message) -> eh::Result<void> {
  return std::unexpected(std::move(message));
}

// Runs a single task with correlation attributes and exception conversion;
// the worker loop owns the results slot and the attempted marker.
auto runOneTask(
  TaskSpec const& task,
  std::size_t slot,
  progress::ProgressContext& progressCtx
) -> eh::Result<void> {
  auto taskCtx = TaskContext{.slot = slot, .progress = progressCtx};

  // Business tasks stamp task_id/input on every record emitted while
  // they run; probe tasks (no input) stay uncorrelated.
  auto attrs = std::optional<logging::ScopedLogAttributes>{};
  if (task.input.has_value()) {
    attrs.emplace(
      std::initializer_list<std::pair<std::string_view, std::string_view>>{
        {std::string_view{"task_id"}, task.id},
        {std::string_view{"input"}, task.input.value()},
      }
    );
  }

  if (!task.run) {
    return makeTaskError(std::format("Task runner is not set: {}", task.id));
  }

  try {
    return task.run(taskCtx);
  } catch (std::exception const& ex) {
    auto const message = std::format("Task {} threw exception: {}", task.id, ex.what());
    LOG_ERROR("{}", message);
    return makeTaskError(message);
  } catch (...) {
    auto const message = std::format("Task {} threw unknown exception", task.id);
    LOG_ERROR("{}", message);
    return makeTaskError(message);
  }
}

}  // namespace

bool TaskContext::stopRequested() const {
  return stopsignal::isStopRequested();
}

std::size_t resolveWorkerCount(std::size_t taskCount, std::size_t maxConcurrency) {
  if (taskCount == 0) { return 0; }
  return std::max<std::size_t>(1, std::min(taskCount, maxConcurrency));
}

auto runTasks(TaskPlan const& plan) -> TaskRunResult {
  auto results = std::vector<eh::Result<void>>(plan.tasks.size());
  auto attempted = std::vector<char>(plan.tasks.size(), 0);
  auto attemptedCount = std::atomic_size_t{0};

  if (plan.tasks.empty()) {
    return TaskRunResult{
      .results = std::move(results),
      .attempted = std::move(attempted),
      .attemptedCount = 0,
      .canceled = false,
    };
  }

  auto localProgress = progress::ProgressContext{};
  auto& progressCtx = plan.progress != nullptr ? *plan.progress : localProgress;
  auto const workerCount = resolveWorkerCount(plan.tasks.size(), plan.maxConcurrency);
  auto nextIndex = std::atomic_size_t{0};
  auto cursorGuard = std::optional<progress::CursorGuard>{};
  if (plan.hideCursor) { cursorGuard.emplace(); }

  parallel::runIndexedTasks(workerCount, workerCount, [&](std::size_t slot) {
    while (true) {
      if (stopsignal::isStopRequested()) { break; }

      auto const taskIndex = nextIndex.fetch_add(1, std::memory_order_acq_rel);
      if (taskIndex >= plan.tasks.size()) { break; }

      attempted[taskIndex] = 1;
      attemptedCount.fetch_add(1, std::memory_order_release);

      results[taskIndex] = runOneTask(plan.tasks[taskIndex], slot, progressCtx);
    }
  });

  return TaskRunResult{
    .results = std::move(results),
    .attempted = std::move(attempted),
    .attemptedCount = attemptedCount.load(std::memory_order_acquire),
    .canceled = stopsignal::isStopRequested(),
  };
}

}  // namespace taskexec
