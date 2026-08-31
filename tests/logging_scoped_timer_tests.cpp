#include "logging/log_tags.h"
#include "logging/logging.h"
#include "test_utils.h"

#include <spdlog/logger.h>

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

// ── File-scoped DEFINE_LOGGER — ScopedTimer's LOG_INFO calls resolve through this ──
DEFINE_LOGGER(logtags::TEST_INFRA);

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — ScopedTimer logs entry message on construction
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("ScopedTimer logs begin on construction", "[logging][scoped_timer]") {
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);
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
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

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
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

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
    CHECK(testutils::countOccurrences(afterMovedReset, "move_stage completed") == 1);
  }

  // Move-from also ensured that the moved-from internal state was updated.
  // Verify: create one, move it, then let both the original (moved-from) and
  // moved-to destruct. The stream should show exactly one begin and one complete.
  auto [logger2, oss2] = testutils::registerCapturingLogger(
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

  CHECK(testutils::countOccurrences(finalOutput, "ownership_test begin") == 1);

  CHECK(testutils::countOccurrences(finalOutput, "ownership_test completed") == 1);
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
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

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

// ─────────────────────────────────────────────────────────────────────────────
// Test 7 — Empty stage name edge case
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("ScopedTimer with empty stage name logs correctly", "[logging][scoped_timer]") {
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

  {
    logging::ScopedTimer timer("");
    logger->flush();
    auto const afterBegin = oss->str();
    CAPTURE(afterBegin);
    // Should produce begin message without crashing
    CHECK(afterBegin.find("begin") != std::string::npos);
  }
  logger->flush();

  auto const output = oss->str();
  CAPTURE(output);
  // Should produce completed message without crashing
  CHECK(output.find("completed in") != std::string::npos);
  CHECK(output.find("ms") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8 — Move assignment operator
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("ScopedTimer move assignment works", "[logging][scoped_timer]") {
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

  {
    logging::ScopedTimer t1("first");
    logging::ScopedTimer t2("second");
    logger->flush();  // flush both begin messages

    // Move-assign t2 into t1. t1 now owns "second", t2 is moved-from.
    t1 = std::move(t2);

    // t2 (moved-from) destructs — should NOT log completion for "second"
    // (goes out of scope at end of this block, but we can't easily isolate
    //  since t1 also goes out of scope)

    // Both t1 and t2 go out of scope here (order: t2 first, then t1)
    // t2 is moved-from — no "second completed" from t2
    // t1 owns "second" — should log exactly one "second completed"
  }
  logger->flush();

  auto const output = oss->str();
  CAPTURE(output);

  // There should be exactly one "first begin" (from t1's original constructor)
  CHECK(testutils::countOccurrences(output, "first begin") == 1);

  // There should be exactly one "second begin" (from t2's constructor)
  CHECK(testutils::countOccurrences(output, "second begin") == 1);

  // There should be exactly one "second completed" (from t1 after move-assign)
  CHECK(testutils::countOccurrences(output, "second completed") == 1);

  // "first completed" should NOT appear (t1 was overwritten by move-assign
  // before it destructed, so it logs "second completed", not "first completed")
  CHECK(output.find("first completed") == std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 9 — Self-move-assignment is safe
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("self-move-assignment is safe", "[logging][scoped_timer]") {
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

  {
    logging::ScopedTimer t("self");
    logger->flush();  // flush begin

    // Self-move-assignment — should be a no-op
    t = std::move(t);
    // t should still be valid and owned (not moved-from)
  }
  logger->flush();

  auto const output = oss->str();
  CAPTURE(output);

  // Should have exactly one "self begin" and one "self completed"
  CHECK(testutils::countOccurrences(output, "self begin") == 1);

  CHECK(testutils::countOccurrences(output, "self completed") == 1);
}
