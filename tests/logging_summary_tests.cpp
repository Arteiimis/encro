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
    .echoEnabled = false,
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
    .echoEnabled = false,
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
