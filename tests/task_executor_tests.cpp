#include "core/task_executor.h"
#include "infra/stop_signal.h"

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

TEST_CASE("resolveWorkerCount clamps concurrency to task count", "[task-executor]") {
  CHECK(taskexec::resolveWorkerCount(0, 4) == 0);
  CHECK(taskexec::resolveWorkerCount(1, 4) == 1);
  CHECK(taskexec::resolveWorkerCount(3, 10) == 3);
  CHECK(taskexec::resolveWorkerCount(8, 2) == 2);
}

TEST_CASE(
  "runTasks executes all tasks within configured concurrency",
  "[task-executor]"
) {
  stopsignal::reset();

  auto active = std::atomic_size_t{0};
  auto peak = std::atomic_size_t{0};
  auto seenSlots = std::vector<std::size_t>(6, 0);

  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(6);
  for (auto index = std::size_t{0}; index < 6; ++index) {
    tasks.push_back(
      taskexec::TaskSpec{
        .id = std::format("task-{}", index),
        .label = std::format("Task {}", index),
        .run = [&, index](taskexec::TaskContext& ctx) -> eh::Result<void> {
          auto const current = active.fetch_add(1, std::memory_order_acq_rel) + 1;
          auto peakNow = peak.load(std::memory_order_acquire);
          while (
            current > peakNow
            && !peak.compare_exchange_weak(
              peakNow,
              current,
              std::memory_order_acq_rel,
              std::memory_order_acquire
            )
          ) { }

          seenSlots[index] = ctx.slot;
          std::this_thread::sleep_for(20ms);
          active.fetch_sub(1, std::memory_order_acq_rel);
          return {};
        }
      }
    );
  }

  auto const result = taskexec::runTasks(
    taskexec::TaskPlan{
      .tasks = std::move(tasks),
      .maxConcurrency = 2,
      .progress = nullptr,
      .hideCursor = false,
    }
  );

  REQUIRE(result.attemptedCount == 6);
  REQUIRE(result.results.size() == 6);
  CHECK_FALSE(result.canceled);
  CHECK(peak.load(std::memory_order_acquire) <= 2);
  CHECK(std::ranges::all_of(result.attempted, [](char attempted) {
    return attempted == 1;
  }));
  CHECK(std::ranges::all_of(result.results, [](auto const& item) {
    return item.has_value();
  }));
  CHECK(std::ranges::all_of(seenSlots, [](std::size_t slot) { return slot < 2; }));
}

TEST_CASE("runTasks preserves task failures", "[task-executor]") {
  stopsignal::reset();

  auto tasks = std::vector<taskexec::TaskSpec>{
    taskexec::TaskSpec{
      .id = "ok-1",
      .label = "ok-1",
      .run = [](taskexec::TaskContext&) -> eh::Result<void> { return {}; },
    },
    taskexec::TaskSpec{
      .id = "fail",
      .label = "fail",
      .run = [](taskexec::TaskContext&) -> eh::Result<void> {
        return eh::makeError("expected failure");
      },
    },
    taskexec::TaskSpec{
      .id = "ok-2",
      .label = "ok-2",
      .run = [](taskexec::TaskContext&) -> eh::Result<void> { return {}; },
    },
  };

  auto const result = taskexec::runTasks(
    taskexec::TaskPlan{
      .tasks = std::move(tasks),
      .maxConcurrency = 3,
      .progress = nullptr,
      .hideCursor = false,
    }
  );

  REQUIRE(result.attemptedCount == 3);
  REQUIRE(result.results.size() == 3);
  CHECK(result.results[0]);
  REQUIRE_FALSE(result.results[1]);
  CHECK(result.results[1].error() == "expected failure");
  CHECK(result.results[2]);
}
