#include "logging/log_tags.h"
#include "logging/logging.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <catch2/catch_all.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// ── Helper: register a test logger with an ostream sink for output capture ──

auto registerCapturingLoggerForContext(char const* name)
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

// ── Helper: RAII guard that clears the TLS context stack before and after test ──

struct ScopedContextReset {
  ScopedContextReset() { logging::detail::resetContextStack(); }
  ~ScopedContextReset() { logging::detail::resetContextStack(); }
  ScopedContextReset(ScopedContextReset const&) = delete;
  auto operator=(ScopedContextReset const&) -> ScopedContextReset& = delete;
};

}  // namespace

// ── File-scoped DEFINE_LOGGER — tests use TEST_INFRA tag ──
DEFINE_LOGGER(logtags::TEST_INFRA);

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — ScopedErrorContext pushes frame on construction
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("ScopedErrorContext pushes frame on construction",
          "[logging][error_context][scoped_error_context]") {
  ScopedContextReset reset;

  auto constexpr kStage = std::string_view{"test_stage"};
  auto constexpr kDetail = std::string_view{"test_detail"};

  logging::ScopedErrorContext ctx(kStage, kDetail);
  auto const chain = logging::detail::formatContextChain();

  CAPTURE(chain);
  CHECK(chain.find("test_stage") != std::string::npos);
  CHECK(chain.find("test_detail") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — ScopedErrorContext pops frame on destruction
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("ScopedErrorContext pops frame on destruction",
          "[logging][error_context][scoped_error_context]") {
  ScopedContextReset reset;

  std::string insideChain;
  {
    logging::ScopedErrorContext ctx("stage", "detail");
    insideChain = logging::detail::formatContextChain();
  }
  auto const outsideChain = logging::detail::formatContextChain();

  CAPTURE(insideChain);
  CAPTURE(outsideChain);
  CHECK_FALSE(insideChain.empty());
  CHECK(outsideChain.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 — ScopedErrorContext is not copyable
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("ScopedErrorContext is not copyable",
          "[logging][error_context][scoped_error_context]") {
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<logging::ScopedErrorContext>);
  STATIC_CHECK_FALSE(std::is_copy_assignable_v<logging::ScopedErrorContext>);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 — ScopedErrorContext destructor is noexcept
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("ScopedErrorContext destructor is noexcept",
          "[logging][error_context][scoped_error_context]") {
  STATIC_CHECK(std::is_nothrow_destructible_v<logging::ScopedErrorContext>);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5 — Moved-from ScopedErrorContext does not double-pop
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Moved-from ScopedErrorContext does not double-pop",
          "[logging][error_context][scoped_error_context]") {
  ScopedContextReset reset;

  {
    logging::ScopedErrorContext original("move_test", "detail");
    // Verify one frame is on the stack after construction
    auto const beforeMove = logging::detail::formatContextChain();
    CHECK_FALSE(beforeMove.empty());

    // Move-construct a new ScopedErrorContext from the original
    logging::ScopedErrorContext moved(std::move(original));

    // Destroy the original (moved-from) — the frame should still be on stack
    // (moved-from destructor is a no-op)
    // We can't easily test this mid-block, so we verify after both destruct

    // Let both go out of scope: first moved-from (original), then moved
    // After moved-from destructor: frame still on stack (no-op)
    // After moved destructor: frame popped
  }

  auto const chain = logging::detail::formatContextChain();
  CAPTURE(chain);
  CHECK(chain.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6 — Nested ScopedErrorContext produces correctly ordered chain
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Nested ScopedErrorContext produces ordered chain",
          "[logging][error_context][scoped_error_context]") {
  ScopedContextReset reset;

  {
    logging::ScopedErrorContext outer("outer", "");
    {
      logging::ScopedErrorContext inner("inner", "value");
      auto const chain = logging::detail::formatContextChain();
      CAPTURE(chain);
      // "outer" should appear before "inner" (left-to-right = push order)
      auto const outerPos = chain.find("outer");
      auto const innerPos = chain.find("inner");
      CHECK(outerPos != std::string::npos);
      CHECK(innerPos != std::string::npos);
      CHECK(outerPos < innerPos);
    }
    // inner popped here
  }
  // outer popped here

  auto const chain = logging::detail::formatContextChain();
  CHECK(chain.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7 — Self-move-assignment is safe
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Self-move-assignment is safe",
          "[logging][error_context][scoped_error_context]") {
  ScopedContextReset reset;

  {
    logging::ScopedErrorContext self("self", "test");
    auto const beforeSelfMove = logging::detail::formatContextChain();
    CHECK_FALSE(beforeSelfMove.empty());

    // Self-move-assignment — should be a no-op (no destruction, no double-pop)
    self = std::move(self);

    auto const afterSelfMove = logging::detail::formatContextChain();
    CHECK_FALSE(afterSelfMove.empty());
    // Frame should still be present (not popped by self-move)
  }

  // After destruction, stack should be empty (exactly one pop occurred)
  auto const chain = logging::detail::formatContextChain();
  CAPTURE(chain);
  CHECK(chain.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8 — Context depth limit 16 frames with truncation
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Context depth limit 16 frames with truncation",
          "[logging][error_context][scoped_error_context]") {
  ScopedContextReset reset;

  {
    // Push 20 frames inside a block — need a container to hold the guards
    std::vector<logging::ScopedErrorContext> guards;
    guards.reserve(20);
    for (auto i = 0; i < 20; ++i) {
      auto const stageStr = std::string{"stage"} + std::to_string(i + 1);
      guards.emplace_back(stageStr, "");
    }

    auto const chain = logging::detail::formatContextChain();
    CAPTURE(chain);

    // Should contain truncation marker for 4 dropped frames
    CHECK(chain.find("[truncated: 4]") != std::string::npos);

    // Should contain the 16 most recent frames (stages 5-20)
    CHECK(chain.find("stage5") != std::string::npos);
    CHECK(chain.find("stage20") != std::string::npos);

    // Should NOT contain the 4 oldest frames (stages 1-4)
    CHECK(chain.find("stage1") == std::string::npos);
    CHECK(chain.find("stage2") == std::string::npos);
    CHECK(chain.find("stage3") == std::string::npos);
    CHECK(chain.find("stage4") == std::string::npos);

    // The truncation marker should appear before the frame content
    auto const truncPos = chain.find("[truncated: 4]");
    auto const frame5Pos = chain.find("stage5");
    CHECK(truncPos < frame5Pos);
  }

  auto const chain = logging::detail::formatContextChain();
  CHECK(chain.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 9 — Empty stage name edge case
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Empty stage name edge case",
          "[logging][error_context][scoped_error_context]") {
  ScopedContextReset reset;

  {
    logging::ScopedErrorContext ctx("", "");
    auto const chain = logging::detail::formatContextChain();
    CAPTURE(chain);
    // Should not crash and should produce a non-empty string
    // (containing at least the context bracket)
    CHECK_FALSE(chain.empty());
    CHECK(chain.find("[context:") != std::string::npos);
  }

  auto const chain = logging::detail::formatContextChain();
  CHECK(chain.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 10 — Context chain format matches D-03/D-04
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Context chain format matches design spec",
          "[logging][error_context][scoped_error_context]") {
  ScopedContextReset reset;

  // Sub-test A: two frames with details → " [context: encode(retry 2/3) > ffmpeg(exit 1)]"
  {
    auto detail1 = std::string{"retry 2/3"};
    auto detail2 = std::string{"exit 1"};
    logging::ScopedErrorContext ctx1("encode", detail1);
    logging::ScopedErrorContext ctx2("ffmpeg", detail2);

    auto const chain = logging::detail::formatContextChain();
    CAPTURE(chain);
    CHECK(chain == " [context: encode(retry 2/3) > ffmpeg(exit 1)]");
  }

  // Sub-test B: detail="" renders as "stage" without empty parentheses
  {
    logging::ScopedErrorContext ctx1("scan", "");
    logging::ScopedErrorContext ctx2("probe", "");

    auto const chain = logging::detail::formatContextChain();
    CAPTURE(chain);
    CHECK(chain == " [context: scan > probe]");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 11 — Empty TLS stack produces empty string
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Empty TLS stack produces empty string",
          "[logging][error_context][scoped_error_context]") {
  ScopedContextReset reset;

  auto const chain = logging::detail::formatContextChain();
  CAPTURE(chain);
  CHECK(chain.empty());
  CHECK(chain == "");
}
