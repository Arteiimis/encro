#pragma once

#include <cstddef>
#include <functional>

namespace parallel {

using IndexedTask = std::function<void(std::size_t)>;

void runIndexedTasks(
  std::size_t taskCount,
  std::size_t workerCount,
  IndexedTask const& task
);

}  // namespace parallel
