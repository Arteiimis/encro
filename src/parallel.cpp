#include "parallel.h"

#include <BS_thread_pool.hpp>

#include <algorithm>

namespace parallel {

void runIndexedTasks(
  std::size_t taskCount,
  std::size_t workerCount,
  IndexedTask const& task
) {
  if (taskCount == 0 || !task) { return; }

  auto const actualWorkers =
    std::max<std::size_t>(1, std::min(taskCount, workerCount));

  auto pool = BS::pause_thread_pool{actualWorkers};
  pool.pause();

  for (auto index = std::size_t{0}; index < taskCount; ++index) {
    pool.detach_task([&task, index] { task(index); });
  }

  pool.unpause();
  pool.wait();
}

}  // namespace parallel
