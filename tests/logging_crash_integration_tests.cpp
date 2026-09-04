#include "infra/crash_runtime.h"
#include "logging/setup.h"
#include "test_utils.h"

#include <boost/json.hpp>  // IWYU pragma: keep

#if defined(_WIN32)
  #include <io.h>  // IWYU pragma: keep -- Windows-only (guarded by _WIN32)
#endif
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE(
  "currentLogFilePath returns valid path after setup",
  "[logging][crash_integration]"
) {
  TempDir temp;

  auto config = logging::LogConfig{
    .colorsEnabled = false,
    .customLogDir = temp.path,
  };

  auto const setupResult = logging::setup(config);
  REQUIRE(setupResult.has_value());

  auto const logPath = logging::currentLogFilePath();
  REQUIRE(logPath.has_value());

  // Both must return the same file path
  CHECK(setupResult.value() == logPath.value());

  // The file must exist on disk
  CHECK(fs::exists(logPath.value()));

  logging::shutdown();
}

TEST_CASE(
  "currentLogFilePath returns nullopt after shutdown",
  "[logging][crash_integration]"
) {
  TempDir temp;

  auto config = logging::LogConfig{
    .colorsEnabled = false,
    .customLogDir = temp.path,
  };

  auto const setupResult = logging::setup(config);
  REQUIRE(setupResult.has_value());

  // Before shutdown, path must be available
  CHECK(logging::currentLogFilePath().has_value());

  logging::shutdown();

  // After shutdown, path must be cleared (crash handler detects this and falls back)
  CHECK(logging::currentLogFilePath() == std::nullopt);
}

TEST_CASE(
  "writeDirectLogLine formats timestamp with millisecond and tz offset",
  "[logging][crash_integration]"
) {
  TempDir temp;

  auto config = logging::LogConfig{
    .colorsEnabled = false,
    .customLogDir = temp.path,
  };

  auto const setupResult = logging::setup(config);
  REQUIRE(setupResult.has_value());
  auto const logPath = setupResult.value();

  auto const testBody = std::string{"direct write format verification"};
  CHECK(crash::writeDirectLogLine(testBody));

  logging::shutdown();

  auto const content = testutils::readTextFile(logPath);
  auto const bodyPos = content.find("] [critical] [infra.crash] " + testBody);
  REQUIRE(bodyPos != std::string::npos);

  // Timestamp segment must match spdlog kLogPattern precision:
  // [YYYY-MM-DDTHH:MM:SS.mmm+HH:MM] — 29 chars (spdlog's %z uses a colon).
  auto const openPos = content.rfind('[', bodyPos);
  REQUIRE(openPos != std::string::npos);
  auto const ts = content.substr(openPos + 1, bodyPos - openPos - 1);
  CAPTURE(ts);
  CHECK(ts.size() == 29);
  CHECK(ts[10] == 'T');
  CHECK(ts[19] == '.');

  // The direct-write timestamp must be structurally identical to a regular
  // spdlog line's timestamp (same length) so same-second lines sort together.
  auto const spdlogTsOpen = content.find('[');
  REQUIRE(spdlogTsOpen != std::string::npos);
  auto const spdlogTsClose = content.find("] [", spdlogTsOpen);
  REQUIRE(spdlogTsClose != std::string::npos);
  CHECK(spdlogTsClose - spdlogTsOpen - 1 == ts.size());
}

TEST_CASE(
  "writeDirectLogLine returns false when file is unwritable and succeeds after restore",
  "[logging][crash_integration]"
) {
  TempDir temp;

  auto config = logging::LogConfig{
    .colorsEnabled = false,
    .customLogDir = temp.path,
  };

  auto const setupResult = logging::setup(config);
  REQUIRE(setupResult.has_value());
  auto const logPath = setupResult.value();

  // Make the file temporarily impossible to open for append: must fail without
  // throwing. On Windows, set the read-only attribute via the CRT (the MSVC
  // fs::permissions implementation is a no-op); on POSIX, remove the write
  // permission.
#if defined(_WIN32)
  REQUIRE(_wchmod(logPath.c_str(), _S_IREAD) == 0);
#else
  fs::permissions(logPath, fs::perms::owner_write, fs::perm_options::remove);
#endif
  CHECK_FALSE(crash::writeDirectLogLine("must not land"));

#if defined(_WIN32)
  REQUIRE(_wchmod(logPath.c_str(), _S_IWRITE) == 0);
#else
  fs::permissions(logPath, fs::perms::owner_write, fs::perm_options::add);
#endif
  CHECK(crash::writeDirectLogLine("lands after restore"));

  logging::shutdown();

  auto const content = testutils::readTextFile(logPath);
  CHECK(content.find("lands after restore") != std::string::npos);
  CHECK(content.find("must not land") == std::string::npos);
}

TEST_CASE(
  "writeDirectLogLine returns false without an active log file",
  "[logging][crash_integration]"
) {
  CHECK_FALSE(crash::writeDirectLogLine("no log file"));
}

// ── RED 7.3 — crash direct write reaches the active format ──────────────────

TEST_CASE(
  "crash direct write lands in .log, and .ndjson only when JSON logging is on",
  "[logging][crash_integration][run_id]"
) {
  TempDir temp;

  auto config = logging::LogConfig{
    .jsonEnabled = false,
    .colorsEnabled = false,
    .customLogDir = temp.path,
  };

  SECTION("json on: .log and .ndjson with matching run id") {
    config.jsonEnabled = true;

    // setRunId AFTER setup: the bootstrap in setup() regenerates the id
    auto const setupResult = logging::setup(config);
    REQUIRE(setupResult.has_value());
    logging::setRunId("crash-test-run-42");

    REQUIRE(crash::writeDirectLogLine("boom"));
    logging::shutdown();

    // .log line carries the run id
    auto const logPath = setupResult.value();
    auto const logContent = testutils::readTextFile(logPath);
    CHECK(logContent.find("boom run_id=crash-test-run-42") != std::string::npos);

    // .ndjson holds a parseable crash record with the same run id
    auto ndjsonWithExt = logPath;
    ndjsonWithExt.replace_extension(".ndjson");
    auto const ndjsonContent = testutils::readTextFile(ndjsonWithExt);

    auto found = false;
    auto start = std::size_t{0};
    for (;;) {
      auto const eol = ndjsonContent.find('\n', start);
      if (eol == std::string::npos) { break; }
      auto ec = boost::system::error_code{};
      auto const parsed =
        boost::json::parse(ndjsonContent.substr(start, eol - start), ec);
      if (!ec && parsed.is_object()) {
        auto const& obj = parsed.as_object();
        if (obj.at("module").as_string() == "infra.crash") {
          found = true;
          CHECK(obj.at("level").as_string() == "critical");
          CHECK(obj.at("run_id").as_string() == "crash-test-run-42");
          CHECK(obj.at("message").as_string() == "boom");
        }
      }
      start = eol + 1;
    }
    CHECK(found);
  }

  SECTION("json off: touches only the .log file") {
    config.jsonEnabled = false;

    // setRunId AFTER setup: the bootstrap in setup() regenerates the id
    auto const setupResult = logging::setup(config);
    REQUIRE(setupResult.has_value());
    logging::setRunId("crash-test-nojson");

    REQUIRE(crash::writeDirectLogLine("boom-no-json"));
    logging::shutdown();

    auto const logPath = setupResult.value();
    auto const logContent = testutils::readTextFile(logPath);
    CHECK(logContent.find("boom-no-json run_id=crash-test-nojson") != std::string::npos);

    auto ndjsonWithExt = logPath;
    ndjsonWithExt.replace_extension(".ndjson");
    CHECK(!fs::exists(ndjsonWithExt));
  }
}
