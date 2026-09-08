#include "logging/setup.h"

#include "test_utils.h"

#include <spdlog/spdlog.h>

#include <boost/json.hpp>        // IWYU pragma: keep

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

auto lastLineOf(fs::path const& file) -> std::string {
  auto stream = std::ifstream{file};
  auto line = std::string{};
  auto last = std::string{};
  while (std::getline(stream, line)) { last = line; }
  return last;
}

auto parseLine(std::string const& line) -> boost::json::object {
  auto const val = boost::json::parse(line);
  REQUIRE(val.is_object());
  return val.as_object();
}

}  // namespace

// ── RED 5.1 — pass-through counting sink ────────────────────────────────────

TEST_CASE("counting sink counts per level and forwards records", "[logging][summary]") {
  TempDir const temp;
  auto const& testDir = temp.path;

  auto const config = logging::LogConfig{
    .echoLevel = 0,
    .jsonEnabled = false,
    .colorsEnabled = false,
    .customLogDir = testDir,
  };

  auto const result = logging::setup(config);
  REQUIRE(result.has_value());

  auto* logger = spdlog::default_logger_raw();
  REQUIRE(logger != nullptr);
  logger->info("forwarded info");
  logger->warn("forwarded warning");
  logger->error("forwarded error");

  logging::shutdown();

  // Records were forwarded to the file sink unchanged
  auto const content = lastLineOf(result.value());
  CHECK(content.find("forwarded error") != std::string::npos);

  // Counts per level
  auto const counts = logging::levelCounts();
  CHECK(counts.at("info") == 1);
  CHECK(counts.at("warning") == 1);
  CHECK(counts.at("error") == 1);
}

// ── RED 5.3 — logRunSummary emits the summary record ────────────────────────

TEST_CASE(
  "logRunSummary emits NDJSON summary record with all fields",
  "[logging][summary]"
) {
  TempDir const temp;
  auto const& testDir = temp.path;

  auto const config = logging::LogConfig{
    .echoLevel = 0,
    .jsonEnabled = true,
    .colorsEnabled = false,
    .customLogDir = testDir,
  };

  auto const result = logging::setup(config);
  REQUIRE(result.has_value());

  logging::logRunSummary(
    logging::SummaryData{
      .status = "success",
      .jobId = "job-123",
      .tasksTotal = 3,
      .tasksFailed = 0,
      .elapsedMs = 1234,
    }
  );

  logging::shutdown();

  auto const ndjsonPath = result.value();
  auto ndjson = ndjsonPath;
  ndjson.replace_extension(".ndjson");
  REQUIRE(fs::exists(ndjson));

  auto const line = lastLineOf(ndjson);
  CAPTURE(line);
  auto const obj = parseLine(line);

  CHECK(obj.contains("summary"));
  auto const& summary = obj.at("summary").as_object();
  CHECK(summary.at("status").as_string() == "success");
  CHECK(summary.at("jobId").as_string() == "job-123");
  CHECK(summary.at("tasks_total").as_int64() == 3);
  CHECK(summary.at("tasks_failed").as_int64() == 0);
  CHECK(summary.at("elapsed_ms").as_int64() == 1234);
  CHECK(summary.at("log").as_string() == ndjsonPath.string());
  CHECK(summary.at("level_counts").is_object());

  // Summary record also carries the run id
  CHECK(obj.contains("run_id"));
  CHECK_FALSE(obj.at("run_id").as_string().empty());

  // Human-readable last line in the .log file
  auto const hrLine = lastLineOf(result.value());
  CAPTURE(hrLine);
  CAPTURE(result.value().string());
  CAPTURE(fs::exists(result.value()));
  CAPTURE(fs::file_size(result.value()));
  CHECK(hrLine.find("RUN SUMMARY:") != std::string::npos);
  CHECK(hrLine.find("status=success") != std::string::npos);
  CHECK(hrLine.find("level_counts={") != std::string::npos);
  CHECK(hrLine.find("log=") != std::string::npos);
}

// ── verbose-levels: echo levels, stream, and format ───────────────────────

TEST_CASE(
  "echo level 1 emits curated short-format diagnostics on the echo stream",
  "[logging][echo]"
) {
  TempDir temp;
  auto echoBuffer = std::make_shared<std::ostringstream>();
  auto echoSink = std::make_shared<spdlog::sinks::ostream_sink_mt>(*echoBuffer);

  auto config = logging::LogConfig{
    .echoLevel = 1,
    .jsonEnabled = false,
    .colorsEnabled = false,
    .customLogDir = temp.path,
    .echoSinkOverride = echoSink,
  };
  REQUIRE(logging::setup(config).has_value());

  // Macro-shaped payloads: LOG_* bake "[file:line]" plus trailing chains, and
  // the short formatter must strip both. Emitted directly on a synchronous
  // logger: other tests register the "test.infra" name, which would make
  // LOG_* macros resolve past the echo sink, and the async queue could race
  // the read below.
  auto syncEcho = std::make_shared<spdlog::logger>("echo-sync-l1", echoSink);
  syncEcho->set_level(spdlog::level::debug);
  auto const fileTag = "logging_summary_tests.cpp";
  syncEcho->info(
    fmt::format(
      "[{}:{}] {}{}",
      fileTag,
      150,
      "hello info",
      " [attrs: {\"task\":\"a.webp\"}]"
    )
  );
  syncEcho->warn(fmt::format("[{}:{}] {}", fileTag, 151, "hello warn"));
  syncEcho->debug(fmt::format("[{}:{}] {}", fileTag, 152, "hello debug"));
  syncEcho->error(fmt::format("[{}:{}] {}", fileTag, 153, "hello error"));
  logging::shutdown();

  auto const echoText = echoBuffer->str();

  // Short format: level name + message; no timestamp, location, or attrs.
  CHECK(echoText.find("info: hello info") != std::string::npos);
  CHECK(echoText.find("warning: hello warn") != std::string::npos);
  CHECK(echoText.find(".cpp:") == std::string::npos);
  CHECK(echoText.find("[attrs:") == std::string::npos);
  CHECK(echoText.find("hello debug") == std::string::npos);  // debug filtered
  CHECK(echoText.find("hello error") == std::string::npos);  // errors own the clean line

  // Setup still produced the run log file alongside the echo stream.
  auto const logFiles = testutils::listRegularFiles(temp.path);
  REQUIRE_FALSE(logFiles.empty());
}

TEST_CASE(
  "echo level 2 emits the full file-log format on the echo stream",
  "[logging][echo]"
) {
  TempDir temp;
  auto echoBuffer = std::make_shared<std::ostringstream>();
  auto echoSink = std::make_shared<spdlog::sinks::ostream_sink_mt>(*echoBuffer);

  auto config = logging::LogConfig{
    .echoLevel = 2,
    .jsonEnabled = false,
    .colorsEnabled = false,
    .customLogDir = temp.path,
    .echoSinkOverride = echoSink,
  };
  REQUIRE(logging::setup(config).has_value());

  auto syncEcho = std::make_shared<spdlog::logger>("echo-sync-l2", echoSink);
  syncEcho->set_level(spdlog::level::debug);

  syncEcho->debug("deep debug detail");
  syncEcho->error("loud error");
  logging::shutdown();

  auto const echoText = echoBuffer->str();
  // Full format: timestamped, with the level tag; debug records included.
  CHECK(echoText.find("deep debug detail") != std::string::npos);
  CHECK(echoText.find("loud error") != std::string::npos);
  CHECK(echoText.find("[debug]") != std::string::npos);
}
