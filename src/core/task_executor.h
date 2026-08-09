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

  auto stopRequested() const -> bool;
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

struct TaskRunResult {
  std::vector<eh::Result<void>> results;
  std::vector<char> attempted;
  std::size_t attemptedCount = 0;
  bool canceled = false;
};

auto resolveWorkerCount(std::size_t taskCount, std::size_t maxConcurrency) -> std::size_t;

auto runTasks(TaskPlan const& plan) -> TaskRunResult;

}  // namespace taskexec
