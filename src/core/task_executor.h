#pragma once

#include "core/error_handle.h"
#include "core/progress.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace taskexec {

struct TaskContext {
  std::size_t slot = 0;
  progress::ProgressContext& progress;
};

struct TaskSpec {
  std::string id;
  std::string label;
  // Set for business tasks (video encode, pack, picture compress); probe/
  // prewarm helper tasks leave it empty so records carry no task correlation.
  std::optional<std::string> input;
  std::function<eh::Result<void>(TaskContext&)> run;
};

struct TaskPlan {
  std::vector<TaskSpec> tasks;
  std::size_t maxConcurrency = 1;
  progress::ProgressContext* progress = nullptr;
  bool hideCursor = false;
};

// Default = not attempted, so a slot the stop signal skipped can never be
// mistaken for a success.
enum class TaskState {
  Skipped,
  Succeeded,
  Failed
};

struct TaskOutcome {
  TaskState state = TaskState::Skipped;
  std::string error;
};

struct TaskRunResult {
  std::vector<TaskOutcome> outcomes;
  std::size_t attemptedCount = 0;
  bool canceled = false;

  auto skippedCount() const -> std::size_t { return outcomes.size() - attemptedCount; }
};

std::size_t resolveWorkerCount(std::size_t taskCount, std::size_t maxConcurrency);

auto runTasks(TaskPlan const& plan) -> TaskRunResult;

}  // namespace taskexec
