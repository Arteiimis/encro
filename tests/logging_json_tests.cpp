#include "logging/json_formatter.h"
#include "logging/log_tags.h"
#include "test_utils.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <boost/json.hpp>        // IWYU pragma: keep

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace {

// Helper: register a test logger with an ostream sink + JsonFormatter for output capture.
// Thin wrapper around testutils::registerCapturingLogger's shared core:
// same capture-stream registry and re-registration, but the sink carries the
// JsonFormatter instead of the "%v" text pattern.
auto registerCapturingLoggerForJson(char const* name)
  -> std::pair<std::shared_ptr<spdlog::logger>, std::ostringstream*> {
  auto oss = std::make_unique<std::ostringstream>();
  auto* ossPtr = oss.get();
  auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(*ossPtr);
  sink->set_formatter(std::make_unique<logging::JsonFormatter>());
  auto logger = std::make_shared<spdlog::logger>(name, sink);
  logger->set_level(spdlog::level::trace);
  logger->flush_on(spdlog::level::trace);
  testutils::reregisterLogger(logger);
  testutils::keepCaptureStreamAlive(std::move(oss));
  return {logger, ossPtr};
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — JsonFormatter emits all fixed fields
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("JsonFormatter emits all fixed fields", "[logging][json]") {
  auto [logger, oss] = registerCapturingLoggerForJson(logtags::TEST_INFRA);

  logger->info("test message");
  logger->flush();

  auto const line = oss->str();
  CAPTURE(line);

  auto const val = boost::json::parse(line);
  REQUIRE(val.is_object());

  auto const& obj = val.as_object();
  CHECK(obj.contains("timestamp"));
  CHECK(obj.contains("level"));
  CHECK(obj.contains("module"));
  CHECK(obj.contains("source"));
  CHECK(obj.contains("message"));

  CHECK(obj.at("level").as_string() == "info");
  CHECK(obj.at("module").as_string() == std::string{logtags::TEST_INFRA});
  CHECK(obj.at("message").as_string().find("test message") != std::string::npos);

  // elapsed_ms absent when message doesn't match "completed in Xms"
  CHECK(!obj.contains("elapsed_ms"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — JsonFormatter emits correct level strings for all levels
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("JsonFormatter emits correct level strings for all levels", "[logging][json]") {
  using LevelPair = std::pair<spdlog::level::level_enum, std::string_view>;

  // Use a single logger for all levels
  auto [logger, oss] = registerCapturingLoggerForJson(logtags::TEST_INFRA);

  // clang-format off
  auto const levels = std::vector<LevelPair>{
    {spdlog::level::trace,    "trace"},
    {spdlog::level::debug,    "debug"},
    {spdlog::level::info,     "info"},
    {spdlog::level::warn,     "warning"},
    {spdlog::level::err,      "error"},
    {spdlog::level::critical, "critical"},
  };
  // clang-format on

  for (auto const& [lvl, expected]: levels) {
    auto oss2 = std::ostringstream{};
    auto sink2 = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss2);
    sink2->set_formatter(std::make_unique<logging::JsonFormatter>());
    auto lvlName = std::string{"level_tester_"} + std::string{expected};
    auto lvlLogger = std::make_shared<spdlog::logger>(lvlName, sink2);
    lvlLogger->set_level(spdlog::level::trace);
    lvlLogger->flush_on(spdlog::level::trace);
    auto existing = spdlog::get(lvlName);
    if (existing != nullptr) { spdlog::drop(lvlName); }
    spdlog::register_logger(lvlLogger);

    lvlLogger->log(lvl, "test");
    lvlLogger->flush();

    auto const line = oss2.str();
    CAPTURE(line);
    CAPTURE(expected);

    auto const val = boost::json::parse(line);
    REQUIRE(val.is_object());
    auto const& obj = val.as_object();
    CHECK(obj.at("level").as_string() == expected);

    spdlog::drop(lvlName);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 — elapsed_ms extracted from ScopedTimer completion message
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("elapsed_ms extracted from ScopedTimer completion message", "[logging][json]") {
  auto [logger, oss] = registerCapturingLoggerForJson(logtags::TEST_INFRA);

  // This mimics ScopedTimer destructor output:
  //   LOG_INFO("{} completed in {}ms", stageName_, elapsed)
  // which produces payload: "[file:line] stageName completed in Xms"
  logger->info("encode completed in 1234ms");
  logger->flush();

  auto const line = oss->str();
  CAPTURE(line);

  auto const val = boost::json::parse(line);
  REQUIRE(val.is_object());
  auto const& obj = val.as_object();

  CHECK(obj.contains("elapsed_ms"));
  CHECK(obj.at("elapsed_ms").as_int64() == 1234);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 — elapsed_ms absent for non-timer messages
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("elapsed_ms absent for non-timer messages", "[logging][json]") {
  auto [logger, oss] = registerCapturingLoggerForJson(logtags::TEST_INFRA);

  logger->info("regular message");
  logger->flush();

  auto const line = oss->str();
  CAPTURE(line);

  auto const val = boost::json::parse(line);
  REQUIRE(val.is_object());
  auto const& obj = val.as_object();

  CHECK(!obj.contains("elapsed_ms"));
  CHECK(obj.contains("message"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5 — error_context extracted from Phase 3 context chain suffix
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "error_context extracted from Phase 3 context chain suffix",
  "[logging][json]"
) {
  auto [logger, oss] = registerCapturingLoggerForJson(logtags::TEST_INFRA);

  // Mimics LOG_ERROR output with context chain appended:
  //   "[video.cpp:100] encode failed [context: input.mkv > encode stage > retry 2/3]"
  logger->error("encode failed [context: input.mkv > encode stage > retry 2/3]");
  logger->flush();

  auto const line = oss->str();
  CAPTURE(line);

  auto const val = boost::json::parse(line);
  REQUIRE(val.is_object());
  auto const& obj = val.as_object();

  CHECK(obj.contains("error_context"));
  auto const& ctx = obj.at("error_context").as_array();
  CHECK(ctx.size() == 3);
  CHECK(ctx[0].as_string() == "input.mkv");
  CHECK(ctx[1].as_string() == "encode stage");
  CHECK(ctx[2].as_string() == "retry 2/3");

  // Message must NOT contain the context suffix
  auto const& msg = obj.at("message").as_string();
  CHECK(msg.find("[context:") == std::string::npos);
  CHECK(msg == "encode failed");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6 — error_context absent for messages without context suffix
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("error_context absent for messages without context suffix", "[logging][json]") {
  auto [logger, oss] = registerCapturingLoggerForJson(logtags::TEST_INFRA);

  logger->error("plain error");
  logger->flush();

  auto const line = oss->str();
  CAPTURE(line);

  auto const val = boost::json::parse(line);
  REQUIRE(val.is_object());
  auto const& obj = val.as_object();

  CHECK(!obj.contains("error_context"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7 — message field preserved verbatim when no context suffix
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("message field preserved verbatim when no context suffix", "[logging][json]") {
  auto [logger, oss] = registerCapturingLoggerForJson(logtags::TEST_INFRA);

  logger->warn("some warning");
  logger->flush();

  auto const line = oss->str();
  CAPTURE(line);

  auto const val = boost::json::parse(line);
  REQUIRE(val.is_object());
  auto const& obj = val.as_object();

  // The message field preserves the raw payload (including "[file:line]" prefix
  // from the LOG_* macros). Context suffix is stripped if present, but this
  // test verifies no unintended modification.
  auto const& msg = obj.at("message").as_string();
  CHECK(msg.find("some warning") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 14 — NDJSON output line ends with newline
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("NDJSON output line ends with newline", "[logging][json]") {
  auto [logger, oss] = registerCapturingLoggerForJson(logtags::TEST_INFRA);

  logger->info("newline test");
  logger->flush();

  auto const output = oss->str();
  CAPTURE(output);

  REQUIRE(!output.empty());
  CHECK(output.back() == '\n');
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 15 — Multiple optional fields coexist
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Multiple optional fields coexist", "[logging][json]") {
  auto [logger, oss] = registerCapturingLoggerForJson(logtags::TEST_INFRA);

  // Message contains BOTH a ScopedTimer completion pattern AND a context chain suffix
  logger->info("encode completed in 5678ms [context: input.mkv > encode]");
  logger->flush();

  auto const line = oss->str();
  CAPTURE(line);

  auto const val = boost::json::parse(line);
  REQUIRE(val.is_object());
  auto const& obj = val.as_object();

  // Both optional fields present
  CHECK(obj.contains("elapsed_ms"));
  CHECK(obj.at("elapsed_ms").as_int64() == 5678);

  CHECK(obj.contains("error_context"));
  auto const& ctx = obj.at("error_context").as_array();
  CHECK(ctx.size() == 2);
  CHECK(ctx[0].as_string() == "input.mkv");
  CHECK(ctx[1].as_string() == "encode");

  // Message has context suffix stripped
  auto const& msg = obj.at("message").as_string();
  CHECK(msg.find("[context:") == std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// RED 1.4 — attrs suffix parsed into top-level correlation fields
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("attrs suffix becomes top-level task_id/input fields", "[logging][json]") {
  auto [logger, oss] = registerCapturingLoggerForJson(logtags::TEST_INFRA);

  // Mimics LOG_INFO output with attribute chain appended
  logger
    ->info(R"(processing [attrs: {"task_id":"encode:a.mkv","input":"C:\\vids\\a.mkv"}])");
  logger->flush();

  auto const line = oss->str();
  CAPTURE(line);

  auto const val = boost::json::parse(line);
  REQUIRE(val.is_object());
  auto const& obj = val.as_object();

  CHECK(obj.contains("task_id"));
  CHECK(obj.at("task_id").as_string() == "encode:a.mkv");
  CHECK(obj.contains("input"));
  CHECK(obj.at("input").as_string() == R"(C:\vids\a.mkv)");

  // Attrs suffix stripped from message
  auto const& msg = obj.at("message").as_string();
  CHECK(msg.find("[attrs:") == std::string::npos);
  CHECK(msg == "processing");
}

TEST_CASE("no attrs fields when suffix absent", "[logging][json]") {
  auto [logger, oss] = registerCapturingLoggerForJson(logtags::TEST_INFRA);

  logger->info("plain message");
  logger->flush();

  auto const line = oss->str();
  CAPTURE(line);

  auto const val = boost::json::parse(line);
  REQUIRE(val.is_object());
  auto const& obj = val.as_object();

  CHECK(!obj.contains("task_id"));
  CHECK(!obj.contains("input"));
}

TEST_CASE("context and attrs suffixes coexist", "[logging][json]") {
  auto [logger, oss] = registerCapturingLoggerForJson(logtags::TEST_INFRA);

  // Mimics LOG_ERROR output: message + context chain + attrs chain (attrs last)
  logger->error(
    R"(encode failed [context: input.mkv > encode] [attrs: {"task_id":"encode:a.mkv","input":"C:\\vids\\a.mkv"}])"
  );
  logger->flush();

  auto const line = oss->str();
  CAPTURE(line);

  auto const val = boost::json::parse(line);
  REQUIRE(val.is_object());
  auto const& obj = val.as_object();

  // error_context intact and NOT swallowing the attrs marker
  CHECK(obj.contains("error_context"));
  auto const& ctx = obj.at("error_context").as_array();
  REQUIRE(ctx.size() == 2);
  CHECK(ctx[0].as_string() == "input.mkv");
  CHECK(ctx[1].as_string() == "encode");

  // Correlation fields present
  CHECK(obj.contains("task_id"));
  CHECK(obj.at("task_id").as_string() == "encode:a.mkv");

  // Message stripped of both suffixes
  auto const& msg = obj.at("message").as_string();
  CHECK(msg == "encode failed");
  CHECK(msg.find("[context:") == std::string::npos);
  CHECK(msg.find("[attrs:") == std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// RED 4.1 — timestamps carry millisecond precision
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("timestamp has millisecond precision", "[logging][json]") {
  auto [logger, oss] = registerCapturingLoggerForJson(logtags::TEST_INFRA);

  logger->info("ms check");
  logger->flush();

  auto const line = oss->str();
  CAPTURE(line);

  auto const val = boost::json::parse(line);
  REQUIRE(val.is_object());
  auto const& obj = val.as_object();

  auto const ts = obj.at("timestamp").as_string();
  // YYYY-MM-DDTHH:MM:SS.sssZ — 24 chars
  CHECK(ts.size() == 24);
  CHECK(ts.ends_with("Z"));
  CHECK(ts[19] == '.');
  CHECK(ts[23] == 'Z');
  for (auto i = std::size_t{20}; i < 23; ++i) { CHECK((ts[i] >= '0' && ts[i] <= '9')); }
}
