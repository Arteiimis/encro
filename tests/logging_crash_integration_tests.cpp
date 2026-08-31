#include "infra/crash_runtime.h"
#include "logging/setup.h"
#include "test_utils.h"

#include <boost/json.hpp>  // IWYU pragma: keep

#include <algorithm>
#include <array>

#if defined(_WIN32)
  #include <io.h>  // IWYU pragma: keep -- Windows-only (guarded by _WIN32)
#endif
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

}  // namespace

TEST_CASE(
  "crash message appears in per-run log file via direct append",
  "[logging][crash_integration]"
) {
  TempDir temp;

  auto config = logging::LogConfig{
    .colorsEnabled = false,
    .customLogDir = temp.path,
  };

  // Setup logging and capture the log file path
  auto const setupResult = logging::setup(config);
  REQUIRE(setupResult.has_value());

  // currentLogFilePath() must return the same path setup() created
  auto const logPath = logging::currentLogFilePath();
  REQUIRE(logPath.has_value());
  CHECK(setupResult.value() == logPath.value());

  // Simulate crash handler behavior: append directly via std::ofstream (bypassing spdlog)
  auto const testMessage = std::string{"[CRASH TEST] direct file append integration"};
  {
    auto ofs = std::ofstream(logPath.value(), std::ios::app);
    REQUIRE(ofs.is_open());
    ofs << testMessage << "\n";
    ofs.close();
  }

  // Verify the message was written to the log file
  auto const content = testutils::readTextFile(logPath.value());
  CHECK(content.find(testMessage) != std::string::npos);

  logging::shutdown();
}

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
  "log file persists on disk after shutdown and remains appendable",
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
  REQUIRE(fs::exists(logPath));

  // Shutdown spdlog — the file must remain on disk
  logging::shutdown();

  // Verify spdlog has cleared the path tracking
  CHECK(logging::currentLogFilePath() == std::nullopt);

  // The file itself must still exist on disk (Pitfall #10: file survives shutdown)
  REQUIRE(fs::exists(logPath));

  // Simulate crash handler: append directly to the persisted file
  auto const crashMessage =
    std::string{"[CRASH POST-SHUTDOWN] message after logger death"};
  {
    auto ofs = std::ofstream(logPath, std::ios::app);
    REQUIRE(ofs.is_open());
    ofs << crashMessage << "\n";
    ofs.close();
  }

  // Verify the crash message appears in the file
  auto const content = testutils::readTextFile(logPath);
  CHECK(content.find(crashMessage) != std::string::npos);
}

TEST_CASE(
  "crash message format includes timestamp level and module tags",
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
  REQUIRE(fs::exists(logPath));

  // Shutdown first to flush spdlog async queue, then verify spdlog wrote lines
  logging::shutdown();

  auto const spdlogContent = testutils::readTextFile(logPath);
  // After shutdown (which flushes), spdlog should have written at least one line
  CHECK(!spdlogContent.empty());
  CHECK(spdlogContent.find('[') != std::string::npos);

  // Now append a crash-style message matching tryWriteDirectToLogFile format (D-15)
  auto const testBody = std::string{"integration test format verification"};
  {
    auto ofs = std::ofstream(logPath, std::ios::app);
    REQUIRE(ofs.is_open());

    auto const now = std::chrono::system_clock::now();
    auto const t = std::chrono::system_clock::to_time_t(now);
    auto tm = std::tm{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    auto tsBuf = std::array<char, 64>{};
    std::strftime(tsBuf.data(), tsBuf.size(), "%Y-%m-%dT%H:%M:%S", &tm);

    auto const formatted =
      std::format("[{}] [critical] [infra.crash] {}\n", tsBuf.data(), testBody);
    ofs.write(formatted.data(), static_cast<std::streamsize>(formatted.size()));
    ofs.close();
  }

  auto const content = testutils::readTextFile(logPath);

  // Message body must be present
  CHECK(content.find(testBody) != std::string::npos);

  // Format must include the required tags (D-15)
  CHECK(content.find("[critical]") != std::string::npos);
  CHECK(content.find("[infra.crash]") != std::string::npos);

  // Timestamp bracket pattern must be present: [...T...] (year starts with "20")
  auto const tsBracketPos = content.find("[20");
  CHECK(tsBracketPos != std::string::npos);
  auto const closeBracketPos = content.find("] ", tsBracketPos);
  CHECK(closeBracketPos != std::string::npos);
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
  CHECK(ts[4] == '-');
  CHECK(ts[7] == '-');
  CHECK(ts[10] == 'T');
  CHECK(ts[13] == ':');
  CHECK(ts[16] == ':');
  CHECK(ts[19] == '.');
  CHECK((ts[23] == '+' || ts[23] == '-'));
  CHECK(std::all_of(ts.begin() + 20, ts.begin() + 23, [](char c) {
    return c >= '0' && c <= '9';
  }));
  CHECK(ts[26] == ':');
  CHECK(std::all_of(ts.begin() + 24, ts.begin() + 26, [](char c) {
    return c >= '0' && c <= '9';
  }));
  CHECK(std::all_of(ts.begin() + 27, ts.end(), [](char c) {
    return c >= '0' && c <= '9';
  }));

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

// ── RED 7.3 — crash direct write reaches both formats ───────────────────────

TEST_CASE(
  "crash direct write lands in .log and .ndjson with matching run id",
  "[logging][crash_integration][run_id]"
) {
  TempDir temp;

  auto config = logging::LogConfig{
    .jsonEnabled = true,
    .colorsEnabled = false,
    .customLogDir = temp.path,
  };

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
    auto const parsed = boost::json::parse(ndjsonContent.substr(start, eol - start), ec);
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

TEST_CASE(
  "crash direct write without JSON logging touches only the .log file",
  "[logging][crash_integration][run_id]"
) {
  TempDir temp;

  auto config = logging::LogConfig{
    .jsonEnabled = false,
    .colorsEnabled = false,
    .customLogDir = temp.path,
  };

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
