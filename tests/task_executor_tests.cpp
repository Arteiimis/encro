#include "core/task_executor.h"
#include "infra/stop_signal.h"
#include "logging/log_tags.h"
#include "logging/logging.h"
#include "test_utils.h"

#include <spdlog/logger.h>  // IWYU pragma: keep -- needed with libstdc++; MSVC pulls it transitively
#include <spdlog/spdlog.h>

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <algorithm>
#include <atomic>
#include <chrono>  // IWYU pragma: keep -- needed with libstdc++; MSVC pulls it transitively
#include <format>
#include <memory>  // IWYU pragma: keep -- needed with libstdc++; MSVC pulls it transitively
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

DEFINE_LOGGER(logtags::TEST_INFRA);

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
    tasks.push_back({
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
        std::this_thread::sleep_for(
          20ms
        );  // sleep-ok: workload shaping for concurrency proof
        active.fetch_sub(1, std::memory_order_acq_rel);
        return {};
      }  //
    });
  }

  auto const result = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = 2,
    .progress = nullptr,
    .hideCursor = false,
  });

  REQUIRE(result.attemptedCount == 6);
  REQUIRE(result.outcomes.size() == 6);
  CHECK_FALSE(result.canceled);
  CHECK(result.skippedCount() == 0);
  CHECK(peak.load(std::memory_order_acquire) <= 2);
  CHECK(std::ranges::all_of(result.outcomes, [](taskexec::TaskOutcome const& outcome) {
    return outcome.state == taskexec::TaskState::Succeeded;
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

  auto const result = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = 3,
    .progress = nullptr,
    .hideCursor = false,
  });

  REQUIRE(result.attemptedCount == 3);
  REQUIRE(result.outcomes.size() == 3);
  CHECK(result.outcomes[0].state == taskexec::TaskState::Succeeded);
  REQUIRE(result.outcomes[1].state == taskexec::TaskState::Failed);
  CHECK(result.outcomes[1].error == "expected failure");
  CHECK(result.outcomes[2].state == taskexec::TaskState::Succeeded);
}

TEST_CASE(
  "runTasks records thrown exceptions with message and logs them",
  "[task-executor]"
) {
  stopsignal::reset();

  auto [logger, oss] = testutils::registerCapturingLogger(logtags::CORE_TASK);

  auto tasks = std::vector<taskexec::TaskSpec>{
    taskexec::TaskSpec{
      .id = "boom",
      .label = "boom",
      .run = [](taskexec::TaskContext&) -> eh::Result<void> {
        throw std::runtime_error{"boom detail"};
      },
    },
    taskexec::TaskSpec{
      .id = "ok",
      .label = "ok",
      .run = [](taskexec::TaskContext&) -> eh::Result<void> { return {}; },
    },
  };

  auto const result = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = 2,
    .progress = nullptr,
    .hideCursor = false,
  });

  REQUIRE(result.outcomes.size() == 2);
  REQUIRE(result.outcomes[0].state == taskexec::TaskState::Failed);
  // The exception message must be part of the recorded failure, not a
  // generic placeholder.
  CHECK(result.outcomes[0].error.find("boom detail") != std::string::npos);
  CHECK(result.outcomes[1].state == taskexec::TaskState::Succeeded);

  // The exception must also reach the log with the task id.
  logger->flush();
  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("boom") != std::string::npos);
  CHECK(output.find("boom detail") != std::string::npos);

  // Cleanup: remove the test logger so later logging::setup() calls (in other
  // test cases) can re-register the full named-logger set without conflicts.
  spdlog::drop(logtags::CORE_TASK);
}

// The stop signal leaves later slots unattempted; a skipped slot must not be
// readable as a success — the trap the outcome shape exists to remove.
TEST_CASE("a slot the stop signal skipped is not a success", "[task-executor]") {
  auto const stopGuard = testutils::ScopedStopSignalReset{};

  auto bodiesStarted = std::atomic_size_t{0};
  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(3);
  for (auto index = std::size_t{0}; index < 3; ++index) {
    tasks.push_back({
      .id = std::format("task-{}", index),
      .label = std::format("Task {}", index),
      .run = [&](taskexec::TaskContext&) -> eh::Result<void> {
        bodiesStarted.fetch_add(1, std::memory_order_acq_rel);
        // maxConcurrency = 1 pins the stop to the gap between the first task
        // and the second: the single worker re-checks the flag before it
        // takes another slot, so tasks 1 and 2 are never attempted.
        stopsignal::requestStop();
        return {};
      },
    });
  }

  auto const result = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = 1,
    .progress = nullptr,
    .hideCursor = false,
  });

  CAPTURE(bodiesStarted.load(std::memory_order_acquire));
  REQUIRE(testutils::waitUntil([&] {
    return bodiesStarted.load(std::memory_order_acquire) == 1;
  }));
  REQUIRE(result.outcomes.size() == 3);
  CHECK(result.outcomes[0].state == taskexec::TaskState::Succeeded);
  CHECK(result.outcomes[1].state == taskexec::TaskState::Skipped);
  CHECK(result.outcomes[2].state == taskexec::TaskState::Skipped);
  CHECK(result.attemptedCount == 1);
  CHECK(result.skippedCount() == 2);
  CHECK(result.canceled);
}

// ── RED 3.2 — tasks with an input stamp task_id/input on records ────────────

TEST_CASE(
  "runTasks logs task_id/input attributes for tasks with an input",
  "[task-executor][run_id]"
) {
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);
  logging::detail::resetAttributeStack();

  auto const plan = taskexec::TaskPlan{
    .tasks =
      {
        taskexec::TaskSpec{
          .id = "encode:vid.mkv",
          .label = "vid.mkv",
          .input = "C:/vids/vid.mkv",
          .run = [](taskexec::TaskContext&) -> eh::Result<void> {
            LOG_INFO("inside task");
            return {};
          },
        },
      },
    .maxConcurrency = 1,
  };
  auto const result = taskexec::runTasks(plan);
  REQUIRE(result.outcomes.size() == 1);
  REQUIRE(result.outcomes[0].state == taskexec::TaskState::Succeeded);

  logger->flush();
  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("[attrs: {") != std::string::npos);
  CHECK(output.find(R"("task_id":"encode:vid.mkv")") != std::string::npos);
  CHECK(output.find(R"("input":"C:/vids/vid.mkv")") != std::string::npos);
}

TEST_CASE(
  "runTasks does not stamp attributes for tasks without an input",
  "[task-executor][run_id]"
) {
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);
  logging::detail::resetAttributeStack();

  auto const plan = taskexec::TaskPlan{
    .tasks =
      {
        taskexec::TaskSpec{
          .id = "probe:vid.mkv",
          .label = "probe",
          .run = [](taskexec::TaskContext&) -> eh::Result<void> {
            LOG_INFO("inside probe task");
            return {};
          },
        },
      },
    .maxConcurrency = 1,
  };
  auto const result = taskexec::runTasks(plan);
  REQUIRE(result.outcomes.size() == 1);
  REQUIRE(result.outcomes[0].state == taskexec::TaskState::Succeeded);

  logger->flush();
  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("inside probe task") != std::string::npos);
  CHECK(output.find("[attrs:") == std::string::npos);
}

TEST_CASE(
  "task attributes do not leak between tasks in one run",
  "[task-executor][run_id]"
) {
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);
  logging::detail::resetAttributeStack();

  // Task 1 carries an input; task 2 (probe-style) runs after it on the same
  // worker thread. Task 2's records must not carry task 1's attributes.
  auto const plan = taskexec::TaskPlan{
    .tasks =
      {
        taskexec::TaskSpec{
          .id = "encode:first.mkv",
          .label = "first.mkv",
          .input = "C:/vids/first.mkv",
          .run = [](taskexec::TaskContext&) -> eh::Result<void> {
            LOG_INFO("first task body");
            return {};
          },
        },
        taskexec::TaskSpec{
          .id = "probe:second",
          .label = "second",
          .run = [](taskexec::TaskContext&) -> eh::Result<void> {
            LOG_INFO("second task body");
            return {};
          },
        },
      },
    .maxConcurrency = 1,
  };
  auto const result = taskexec::runTasks(plan);
  REQUIRE(result.outcomes.size() == 2);

  logger->flush();
  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("first task body") != std::string::npos);
  CHECK(output.find("second task body") != std::string::npos);
  // The second task's line must not carry the first task's attributes
  auto const secondPos = output.find("second task body");
  auto const lineEnd = output.find('\n', secondPos);
  auto const secondLine = output.substr(secondPos, lineEnd - secondPos);
  CHECK(secondLine.find("[attrs:") == std::string::npos);
}

// A caller that wants per-completion bookkeeping (a bar it counts, a rate it
// computes) should read the executor's count instead of wrapping every task.
TEST_CASE(
  "onTaskFinished runs once per finished task and reaches the total",
  "[task-executor]"
) {
  stopsignal::reset();

  auto mutex = std::mutex{};
  auto calls = std::vector<std::pair<std::size_t, std::size_t>>{};

  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(3);
  for (auto index = std::size_t{0}; index < 3; ++index) {
    tasks.push_back({
      .id = std::format("task-{}", index),
      .label = std::format("Task {}", index),
      .run = [](taskexec::TaskContext&) -> eh::Result<void> { return {}; },
    });
  }

  auto const result = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = 1,
    .progress = nullptr,
    .hideCursor = false,
    .onTaskFinished = [&](std::size_t done, std::size_t total) {
      auto const lock = std::lock_guard{mutex};
      calls.emplace_back(done, total);
    },
  });

  REQUIRE(result.attemptedCount == 3);
  auto const lock = std::lock_guard{mutex};
  REQUIRE(calls.size() == 3);
  // `done` counts finished tasks, so it reaches the total without repeating.
  CHECK(calls[0] == std::pair{std::size_t{1}, std::size_t{3}});
  CHECK(calls[1] == std::pair{std::size_t{2}, std::size_t{3}});
  CHECK(calls[2] == std::pair{std::size_t{3}, std::size_t{3}});
}

// With overlapping workers a count of started tasks would report the total
// before anything finished, and a caller's rate (done / elapsed) would be
// wrong from the first completion.
TEST_CASE("onTaskFinished counts finished tasks, not started ones", "[task-executor]") {
  stopsignal::reset();

  auto mutex = std::mutex{};
  auto calls = std::vector<std::size_t>{};
  auto secondStarted = std::atomic_bool{false};
  auto firstFinished = std::atomic_bool{false};

  auto tasks = std::vector<taskexec::TaskSpec>{
    taskexec::TaskSpec{
      .id = "first",
      .label = "first",
      .run = [&](taskexec::TaskContext&) -> eh::Result<void> {
        // Hold this slot open until the other task is in flight too, so both
        // workers are busy at the same time.
        if (!testutils::waitUntil([&] {
              return secondStarted.load(std::memory_order_acquire);
            })) {
          return eh::makeError("second task never started");
        }
        firstFinished.store(true, std::memory_order_release);
        return {};
      },
    },
    taskexec::TaskSpec{
      .id = "second",
      .label = "second",
      .run = [&](taskexec::TaskContext&) -> eh::Result<void> {
        secondStarted.store(true, std::memory_order_release);
        if (!testutils::waitUntil([&] {
              return firstFinished.load(std::memory_order_acquire);
            })) {
          return eh::makeError("first task never finished");
        }
        return {};
      },
    },
  };

  auto const result = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = 2,
    .progress = nullptr,
    .hideCursor = false,
    .onTaskFinished = [&](std::size_t done, std::size_t) {
      auto const lock = std::lock_guard{mutex};
      calls.push_back(done);
    },
  });

  REQUIRE(result.attemptedCount == 2);
  auto const lock = std::lock_guard{mutex};
  REQUIRE(calls.size() == 2);
  std::ranges::sort(calls);
  CHECK(calls[0] == std::size_t{1});
  CHECK(calls[1] == std::size_t{2});
}

TEST_CASE(
  "onTaskFinished is not called for a slot the stop signal skipped",
  "[task-executor]"
) {
  auto const stopGuard = testutils::ScopedStopSignalReset{};

  auto mutex = std::mutex{};
  auto calls = std::vector<std::pair<std::size_t, std::size_t>>{};
  auto bodiesStarted = std::atomic_size_t{0};

  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(3);
  for (auto index = std::size_t{0}; index < 3; ++index) {
    tasks.push_back({
      .id = std::format("task-{}", index),
      .label = std::format("Task {}", index),
      .run = [&](taskexec::TaskContext&) -> eh::Result<void> {
        bodiesStarted.fetch_add(1, std::memory_order_acq_rel);
        stopsignal::requestStop();
        return {};
      },
    });
  }

  auto const result = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = 1,
    .progress = nullptr,
    .hideCursor = false,
    .onTaskFinished = [&](std::size_t done, std::size_t total) {
      auto const lock = std::lock_guard{mutex};
      calls.emplace_back(done, total);
    },
  });

  REQUIRE(result.outcomes.size() == 3);
  CHECK(result.attemptedCount == 1);
  auto const lock = std::lock_guard{mutex};
  // Only the task that ran reached the hook; a skipped slot is not a
  // completion, so the caller's bar can never claim work it never did.
  REQUIRE(calls.size() == 1);
  CHECK(calls[0] == std::pair{std::size_t{1}, std::size_t{3}});
}

TEST_CASE("a throwing onTaskFinished callback does not fail the run", "[task-executor]") {
  stopsignal::reset();

  auto [logger, oss] = testutils::registerCapturingLogger(logtags::CORE_TASK);
  auto calls = std::atomic_size_t{0};

  auto tasks = std::vector<taskexec::TaskSpec>{
    taskexec::TaskSpec{
      .id = "ok",
      .label = "ok",
      .run = [](taskexec::TaskContext&) -> eh::Result<void> { return {}; },
    },
    taskexec::TaskSpec{
      .id = "fail",
      .label = "fail",
      .run = [](taskexec::TaskContext&) -> eh::Result<void> {
        return eh::makeError("expected failure");
      },
    },
  };

  auto const result = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = 1,
    .progress = nullptr,
    .hideCursor = false,
    .onTaskFinished = [&](std::size_t, std::size_t) {
      calls.fetch_add(1, std::memory_order_acq_rel);
      throw std::runtime_error{"hook boom"};
    },
  });

  // The outcome is written before the hook runs, so a throwing callback can
  // neither lose it nor take a pool thread down with it.
  REQUIRE(result.outcomes.size() == 2);
  CHECK(result.outcomes[0].state == taskexec::TaskState::Succeeded);
  REQUIRE(result.outcomes[1].state == taskexec::TaskState::Failed);
  CHECK(result.outcomes[1].error == "expected failure");
  CHECK(calls.load(std::memory_order_acquire) == 2);

  logger->flush();
  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("hook boom") != std::string::npos);

  spdlog::drop(logtags::CORE_TASK);
}
