#include "core/task_executor.h"
#include "infra/stop_signal.h"
#include "logging/log_tags.h"
#include "logging/logging.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

// ── Helper: register a test logger with an ostream sink for output capture ──
// (mirrors tests/logging_infra_test.cpp)

auto registerCapturingLogger(char const* name)
  -> std::pair<std::shared_ptr<spdlog::logger>, std::ostringstream*> {
  static auto sstreams = std::vector<std::unique_ptr<std::ostringstream>>{};
  auto oss = std::make_unique<std::ostringstream>();
  auto* ossPtr = oss.get();
  sstreams.push_back(std::move(oss));

  auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(*ossPtr);
  auto logger = std::make_shared<spdlog::logger>(name, sink);
  logger->set_pattern("%v");
  logger->set_level(spdlog::level::trace);
  logger->flush_on(spdlog::level::trace);

  auto existing = spdlog::get(name);
  if (existing != nullptr) { spdlog::drop(name); }

  spdlog::register_logger(logger);
  return {logger, ossPtr};
}

}  // namespace

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
        std::this_thread::sleep_for(20ms);
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

  auto const result = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = 3,
    .progress = nullptr,
    .hideCursor = false,
  });

  REQUIRE(result.attemptedCount == 3);
  REQUIRE(result.results.size() == 3);
  CHECK(result.results[0]);
  REQUIRE_FALSE(result.results[1]);
  CHECK(result.results[1].error() == "expected failure");
  CHECK(result.results[2]);
}

TEST_CASE(
  "runTasks records thrown exceptions with message and logs them",
  "[task-executor]"
) {
  stopsignal::reset();

  auto [logger, oss] = registerCapturingLogger(logtags::CORE_TASK);

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

  REQUIRE(result.results.size() == 2);
  REQUIRE_FALSE(result.results[0]);
  // The exception message must be part of the recorded failure, not a
  // generic placeholder.
  CHECK(result.results[0].error().find("boom detail") != std::string::npos);
  CHECK(result.results[1]);

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
