#include "logging/log_tags.h"
#include "logging/logging.h"
#include "test_utils.h"

#include <boost/json.hpp>  // IWYU pragma: keep

#include <spdlog/logger.h>  // IWYU pragma: keep -- needed with libstdc++; MSVC pulls it transitively

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// ── Helper: RAII guard that clears the TLS context stack before and after test ──

struct ScopedContextReset {
  ScopedContextReset() {
    logging::detail::resetContextStack();
    logging::detail::resetAttributeStack();
  }
  ~ScopedContextReset() {
    logging::detail::resetContextStack();
    logging::detail::resetAttributeStack();
  }
  ScopedContextReset(ScopedContextReset const&) = delete;
  auto operator=(ScopedContextReset const&) -> ScopedContextReset& = delete;
};

}  // namespace

// ── File-scoped DEFINE_LOGGER — tests use TEST_INFRA tag ──
DEFINE_LOGGER(logtags::TEST_INFRA);

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — ScopedErrorContext pushes frame on construction
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "ScopedErrorContext pushes frame on construction",
  "[logging][error_context][scoped_error_context]"
) {
  ScopedContextReset reset;

  constexpr auto kStage = std::string_view{"test_stage"};
  constexpr auto kDetail = std::string_view{"test_detail"};

  logging::ScopedErrorContext ctx(kStage, kDetail);
  auto const chain = logging::detail::formatContextChain();

  CAPTURE(chain);
  CHECK(chain.find("test_stage") != std::string::npos);
  CHECK(chain.find("test_detail") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — ScopedErrorContext pops frame on destruction
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "ScopedErrorContext pops frame on destruction",
  "[logging][error_context][scoped_error_context]"
) {
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

TEST_CASE(
  "ScopedErrorContext is not copyable",
  "[logging][error_context][scoped_error_context]"
) {
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<logging::ScopedErrorContext>);
  STATIC_CHECK_FALSE(std::is_copy_assignable_v<logging::ScopedErrorContext>);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 — ScopedErrorContext destructor is noexcept
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "ScopedErrorContext destructor is noexcept",
  "[logging][error_context][scoped_error_context]"
) {
  STATIC_CHECK(std::is_nothrow_destructible_v<logging::ScopedErrorContext>);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5 — Moved-from ScopedErrorContext does not double-pop
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "Moved-from ScopedErrorContext does not double-pop",
  "[logging][error_context][scoped_error_context]"
) {
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

TEST_CASE(
  "Nested ScopedErrorContext produces ordered chain",
  "[logging][error_context][scoped_error_context]"
) {
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

TEST_CASE(
  "Self-move-assignment is safe",
  "[logging][error_context][scoped_error_context]"
) {
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

TEST_CASE(
  "Context depth limit 16 frames with truncation",
  "[logging][error_context][scoped_error_context]"
) {
  ScopedContextReset reset;

  {
    // Push 20 frames inside a block — need a container to hold the guards.
    // Stage strings must outlive the ScopedErrorContext instances because
    // ContextFrame stores std::string_view (zero-copy, not owned).
    // Zero-padded names prevent substring false matches (e.g., "stage1" in "stage10").
    std::vector<std::string> stageStrings;
    stageStrings.reserve(20);
    for (auto i = 0; i < 20; ++i) {
      auto const num = i + 1;
      auto const padded = (num < 10) ? "0" + std::to_string(num) : std::to_string(num);
      stageStrings.push_back(std::string{"s"} + padded);
    }

    std::vector<logging::ScopedErrorContext> guards;
    guards.reserve(20);
    for (auto i = 0; i < 20; ++i) { guards.emplace_back(stageStrings[i], ""); }

    auto const chain = logging::detail::formatContextChain();
    CAPTURE(chain);

    // Should contain truncation marker for 4 dropped frames
    CHECK(chain.find("[truncated: 4]") != std::string::npos);

    // Should contain the 16 most recent frames (stages 5-20)
    CHECK(chain.find("s05") != std::string::npos);
    CHECK(chain.find("s20") != std::string::npos);

    // Should NOT contain the 4 oldest frames (stages 1-4)
    CHECK(chain.find("s01") == std::string::npos);
    CHECK(chain.find("s02") == std::string::npos);
    CHECK(chain.find("s03") == std::string::npos);
    CHECK(chain.find("s04") == std::string::npos);

    // The truncation marker should appear before the frame content
    auto const truncPos = chain.find("[truncated: 4]");
    auto const frame5Pos = chain.find("s05");
    CHECK(truncPos < frame5Pos);
  }

  auto const chain = logging::detail::formatContextChain();
  CHECK(chain.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 9 — Empty stage name edge case
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "Empty stage name edge case",
  "[logging][error_context][scoped_error_context]"
) {
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

TEST_CASE(
  "Context chain format matches design spec",
  "[logging][error_context][scoped_error_context]"
) {
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

TEST_CASE(
  "Empty TLS stack produces empty string",
  "[logging][error_context][scoped_error_context]"
) {
  ScopedContextReset reset;

  auto const chain = logging::detail::formatContextChain();
  CAPTURE(chain);
  CHECK(chain.empty());
  CHECK(chain == "");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 12 — LOG_ERROR appends context chain when ScopedErrorContext is active
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("LOG_ERROR appends context chain", "[logging][error_context]") {
  ScopedContextReset reset;
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

  logging::ScopedErrorContext ctx("encode", "input.mkv");
  LOG_ERROR("ffmpeg failed");
  logger->flush();

  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("ffmpeg failed") != std::string::npos);
  CHECK(output.find("[context: encode(input.mkv)]") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 13 — LOG_ERROR without context produces no context suffix
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("LOG_ERROR without context has no suffix", "[logging][error_context]") {
  ScopedContextReset reset;
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

  LOG_ERROR("plain error");
  logger->flush();

  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("plain error") != std::string::npos);
  CHECK(output.find("[context:") == std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 14 — LOG_ERROR with nested context produces ordered chain
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "LOG_ERROR with nested context produces ordered chain",
  "[logging][error_context]"
) {
  ScopedContextReset reset;
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

  {
    logging::ScopedErrorContext ctx1("outer", "");
    {
      auto detail2 = std::string{"file.mkv"};
      logging::ScopedErrorContext ctx2("inner", detail2);
      LOG_ERROR("fail");
      logger->flush();
    }
  }

  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("[context: outer > inner(file.mkv)]") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 15 — LOG_CRITICAL appends context chain
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("LOG_CRITICAL appends context chain", "[logging][error_context]") {
  ScopedContextReset reset;
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

  logging::ScopedErrorContext ctx("critical_stage", "details");
  LOG_CRITICAL("catastrophic failure");
  logger->flush();

  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("catastrophic failure") != std::string::npos);
  CHECK(output.find("[context: critical_stage(details)]") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 16 — LOG_INFO does NOT append context chain
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("LOG_INFO does not append context chain", "[logging][error_context]") {
  ScopedContextReset reset;
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

  logging::ScopedErrorContext ctx("encode", "test.mkv");
  LOG_INFO("info message");
  logger->flush();

  auto const output = oss->str();
  CAPTURE(output);
  CHECK(output.find("info message") != std::string::npos);
  CHECK(output.find("[context:") == std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 17 — ScopedLogAttributes serializes as [attrs: {...}] suffix (RED 1.1)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "ScopedLogAttributes serializes as attrs suffix",
  "[logging][error_context][scoped_log_attributes]"
) {
  ScopedContextReset reset;

  logging::ScopedLogAttributes
    attrs({{"task_id", "encode:a.mkv"}, {"input", R"(C:\vids\a.mkv)"}});
  auto const chain = logging::detail::formatAttributeChain();

  CAPTURE(chain);
  CHECK(chain.starts_with(" [attrs: {"));
  CHECK(chain.ends_with("}]"));
  CHECK(chain.find(R"("task_id":"encode:a.mkv")") != std::string::npos);
  CHECK(chain.find(R"("input":"C:\\vids\\a.mkv")") != std::string::npos);

  // The serialized object must be valid JSON after stripping the wrapper
  namespace json = boost::json;
  auto const objectText =
    chain.substr(9, chain.size() - 10);  // strip " [attrs: " and the trailing "]"
  auto const parsed = json::parse(objectText);
  CHECK(parsed.is_object());
  CHECK(parsed.as_object().at("task_id").as_string() == "encode:a.mkv");
  CHECK(parsed.as_object().at("input").as_string() == R"(C:\vids\a.mkv)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 18 — Attrs frames pop on destruction
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "ScopedLogAttributes pops on destruction",
  "[logging][error_context][scoped_log_attributes]"
) {
  ScopedContextReset reset;

  std::string inside;
  {
    logging::ScopedLogAttributes attrs({{"task_id", "t1"}});
    inside = logging::detail::formatAttributeChain();
  }
  auto const outside = logging::detail::formatAttributeChain();

  CAPTURE(inside);
  CAPTURE(outside);
  CHECK_FALSE(inside.empty());
  CHECK(outside.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 19 — Innermost attribute shadows outer keys
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "Innermost attribute shadows outer keys",
  "[logging][error_context][scoped_log_attributes]"
) {
  ScopedContextReset reset;

  logging::ScopedLogAttributes outer({{"task_id", "outer"}, {"input", "outer-file"}});
  {
    logging::ScopedLogAttributes inner({{"task_id", "inner"}});
    auto const chain = logging::detail::formatAttributeChain();
    CAPTURE(chain);
    CHECK(chain.find(R"("task_id":"inner")") != std::string::npos);
    CHECK(chain.find(R"("task_id":"outer")") == std::string::npos);
    CHECK(chain.find(R"("input":"outer-file")") != std::string::npos);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 20 — Attribute stack caps at 16 frames with FIFO eviction
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "Attribute stack caps at 16 frames with FIFO eviction",
  "[logging][error_context][scoped_log_attributes]"
) {
  ScopedContextReset reset;

  auto guards = std::vector<std::unique_ptr<logging::ScopedLogAttributes>>{};
  guards.reserve(20);
  // string_views must outlive the guards; keep backing strings stable
  auto keys = std::vector<std::string>{};
  keys.reserve(20);
  for (auto i = 0; i < 20; ++i) {
    keys.push_back(std::to_string(i));
    auto const& key = keys[i];
    guards.push_back(
      std::make_unique<logging::ScopedLogAttributes>(
        std::initializer_list<std::pair<std::string_view, std::string_view>>{
          {key, key},
        }
      )
    );
  }

  auto const chain = logging::detail::formatAttributeChain();
  CAPTURE(chain);
  CHECK(chain.find(R"("0":"0")") == std::string::npos);    // oldest evicted
  CHECK(chain.find(R"("19":"19")") != std::string::npos);  // newest retained

  // Reset helper leaves a clean stack (no leak across test cases)
  logging::detail::resetAttributeStack();
  CHECK(logging::detail::formatAttributeChain().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 21 — Attribute values with quotes and control chars are JSON-escaped
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "Attribute values with quotes are JSON-escaped",
  "[logging][error_context][scoped_log_attributes]"
) {
  ScopedContextReset reset;

  logging::ScopedLogAttributes attrs({{"input", R"(C:\dir with "quotes"\a"b.mkv)"}});
  auto const chain = logging::detail::formatAttributeChain();
  CAPTURE(chain);
  CHECK(chain.find(R"(\")") != std::string::npos);
  CHECK(chain.find("quotes") != std::string::npos);
  CHECK(chain.find("a\"b") == std::string::npos);  // raw quote must not appear

  namespace json = boost::json;
  auto const objectText = chain.substr(9, chain.size() - 10);
  auto const parsed = json::parse(objectText);
  CHECK(parsed.as_object().at("input").as_string() == R"(C:\dir with "quotes"\a"b.mkv)");
}
