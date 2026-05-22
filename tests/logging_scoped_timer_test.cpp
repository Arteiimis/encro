#include "logging/log_tags.h"
#include "logging/logging.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <catch2/catch_all.hpp>

#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

// ── Helper: register a test logger with an ostream sink for output capture ──

auto registerCapturingLoggerForTimer(char const* name)
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

  // drop if already registered (from a previous test case)
  auto existing = spdlog::get(name);
  if (existing != nullptr) { spdlog::drop(name); }

  spdlog::register_logger(logger);
  return {logger, ossPtr};
}

}  // namespace

// ── File-scoped DEFINE_LOGGER — ScopedTimer's LOG_INFO calls resolve through this ──
DEFINE_LOGGER(logtags::TEST_INFRA);

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — ScopedTimer logs entry message on construction
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("ScopedTimer logs begin on construction", "[logging][scoped_timer]") {
  auto [logger, oss] = registerCapturingLoggerForTimer(logtags::TEST_INFRA);
  REQUIRE(loggerPtr() != nullptr);

  logging::ScopedTimer timer("test_stage");
  logger->flush();

  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("test_stage begin") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — ScopedTimer logs elapsed time on destruction
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("ScopedTimer logs elapsed time on destruction", "[logging][scoped_timer]") {
  auto [logger, oss] = registerCapturingLoggerForTimer(logtags::TEST_INFRA);

  {
    logging::ScopedTimer timer("elapsed_stage");
    logger->flush();
    auto const afterBegin = oss->str();
    CAPTURE(afterBegin);
    // begin message should appear before completion
    CHECK(afterBegin.find("elapsed_stage begin") != std::string::npos);
  }
  // timer destroyed here — completion message should be queued
  logger->flush();

  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("elapsed_stage completed in") != std::string::npos);
  CHECK(output.find("ms") != std::string::npos);

  // Verify begin appears before completion in the output stream
  auto const beginPos = output.find("elapsed_stage begin");
  auto const completePos = output.find("elapsed_stage completed in");
  CHECK(beginPos < completePos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 — Move semantics transfer timing ownership
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("ScopedTimer move transfers ownership", "[logging][scoped_timer]") {
  auto [logger, oss] = registerCapturingLoggerForTimer(logtags::TEST_INFRA);

  {
    std::optional<logging::ScopedTimer> original(std::in_place, "move_stage");
    logger->flush();  // flush begin message

    // Move-construct a new ScopedTimer from the original
    std::optional<logging::ScopedTimer> moved(std::move(*original));

    // Destroy the original (moved-from) — should NOT log completion
    original.reset();
    logger->flush();
    auto const afterOriginalReset = oss->str();

    // Destroy the moved-to — SHOULD log exactly one completion
    moved.reset();
    logger->flush();
    auto const afterMovedReset = oss->str();

    CAPTURE(afterOriginalReset);
    CAPTURE(afterMovedReset);

    // After original reset, "completed" should NOT appear
    CHECK(afterOriginalReset.find("move_stage completed") == std::string::npos);

    // After moved reset, exactly one "completed" message should appear
    auto completeCount = std::size_t{0};
    auto pos = afterMovedReset.find("move_stage completed");
    while (pos != std::string::npos) {
      ++completeCount;
      pos = afterMovedReset.find("move_stage completed", pos + 1);
    }
    CHECK(completeCount == 1);
  }

  // Move-from also ensured that the moved-from internal state was updated.
  // Verify: create one, move it, then let both the original (moved-from) and
  // moved-to destruct. The stream should show exactly one begin and one complete.
  auto [logger2, oss2] = registerCapturingLoggerForTimer(
    logtags::TEST_INFRA
  );  // re-register for clean slate

  {
    logging::ScopedTimer t1("ownership_test");
    logging::ScopedTimer t2(std::move(t1));
    // t1 is moved-from, t2 owns the timing
  }
  logger->flush();

  auto const finalOutput = oss2->str();
  CAPTURE(finalOutput);

  auto beginCount = std::size_t{0};
  auto bpos = finalOutput.find("ownership_test begin");
  while (bpos != std::string::npos) {
    ++beginCount;
    bpos = finalOutput.find("ownership_test begin", bpos + 1);
  }
  CHECK(beginCount == 1);

  auto endCount = std::size_t{0};
  auto epos = finalOutput.find("ownership_test completed");
  while (epos != std::string::npos) {
    ++endCount;
    epos = finalOutput.find("ownership_test completed", epos + 1);
  }
  CHECK(endCount == 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 — ScopedTimer is not copyable
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("ScopedTimer is not copyable", "[logging][scoped_timer]") {
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<logging::ScopedTimer>);
  STATIC_CHECK_FALSE(std::is_copy_assignable_v<logging::ScopedTimer>);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5 — Nested ScopedTimer produces correct ordering
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Nested ScopedTimer produces correct ordering", "[logging][scoped_timer]") {
  auto [logger, oss] = registerCapturingLoggerForTimer(logtags::TEST_INFRA);

  {
    logging::ScopedTimer outer("outer");
    logger->flush();  // flush outer begin

    {
      logging::ScopedTimer inner("inner");
      logger->flush();  // flush inner begin
      // inner destructor runs here (end of inner scope)
    }
    logger->flush();  // flush inner complete

    // outer destructor runs here (end of outer scope)
  }
  logger->flush();  // flush outer complete

  auto const output = oss->str();
  CAPTURE(output);

  auto const outerBegin = output.find("outer begin");
  auto const innerBegin = output.find("inner begin");
  auto const innerComplete = output.find("inner completed in");
  auto const outerComplete = output.find("outer completed in");

  // All four messages should be present
  CHECK(outerBegin != std::string::npos);
  CHECK(innerBegin != std::string::npos);
  CHECK(innerComplete != std::string::npos);
  CHECK(outerComplete != std::string::npos);

  // Ordering: outer begin < inner begin < inner complete < outer complete
  CHECK(outerBegin < innerBegin);
  CHECK(innerBegin < innerComplete);
  CHECK(innerComplete < outerComplete);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6 — ScopedTimer destructor is noexcept
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("ScopedTimer destructor is noexcept", "[logging][scoped_timer]") {
  STATIC_CHECK(std::is_nothrow_destructible_v<logging::ScopedTimer>);
}
