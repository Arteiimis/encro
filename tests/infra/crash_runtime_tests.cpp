#include "infra/crash_runtime.h"
#include "test_utils.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/json.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
namespace bp = boost::process::v2;

namespace {

auto readText(fs::path const& filePath) -> std::string {
  auto input = std::ifstream{filePath};
  REQUIRE(input.is_open());
  return std::string{std::istreambuf_iterator<char>{input}, {}};
}

class ScopedDefaultLogger {
public:
  explicit ScopedDefaultLogger(std::shared_ptr<spdlog::logger> logger)
    : previous_(spdlog::default_logger()) {
    spdlog::set_default_logger(std::move(logger));
  }

  ~ScopedDefaultLogger() { spdlog::set_default_logger(previous_); }

private:
  std::shared_ptr<spdlog::logger> previous_;
};

auto readProcessStream(boost::asio::readable_pipe& stream) -> std::string {
  auto result = std::string{};
  auto buffer = std::array<char, 4096>{};
  for (;;) {
    boost::system::error_code ec;
    auto const count = stream.read_some(boost::asio::buffer(buffer), ec);
    result.append(buffer.data(), count);
    if (ec || count == 0) { break; }
  }
  return result;
}

}  // namespace

TEST_CASE("reportCaughtException writes crash report to default logger", "[crash]") {
  TempDir temp;
  auto const logPath = temp.path / "crash.log";

  auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
  auto logger = std::make_shared<spdlog::logger>("crash-test", sink);
  logger->set_level(spdlog::level::trace);

  auto guard = ScopedDefaultLogger(logger);
  auto const ex = std::runtime_error{"boom"};
  crash::reportCaughtException("unit-test", ex);
  logger->flush();

  auto const content = readText(logPath);
  CHECK(content.find("[CRASH]") != std::string::npos);
  CHECK(content.find("unit-test: boom") != std::string::npos);
  CHECK(content.find("stacktrace") != std::string::npos);
}

TEST_CASE("reportUnknownException writes stacktrace section", "[crash]") {
  TempDir temp;
  auto const logPath = temp.path / "unknown.log";

  auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
  auto logger = std::make_shared<spdlog::logger>("crash-unknown", sink);
  logger->set_level(spdlog::level::trace);

  auto guard = ScopedDefaultLogger(logger);
  crash::reportUnknownException("top-level");
  logger->flush();

  auto const content = readText(logPath);
  CHECK(content.find("top-level: unknown exception") != std::string::npos);
  CHECK(content.find("[CRASH] stacktrace:") != std::string::npos);
  auto const hasFrames = content.find("#00") != std::string::npos;
  auto const hasEmptyMark = content.find("<empty stacktrace>") != std::string::npos;
  if (!hasFrames) {
    CHECK(hasEmptyMark);
  } else {
    CHECK(hasFrames);
  }
}

TEST_CASE("crash runtime handles real process crash", "[crash][integration]") {
  auto const self = boost::dll::program_location();

  auto ctx = boost::asio::io_context{};
  auto childOut = boost::asio::readable_pipe{ctx};
  auto childErr = boost::asio::readable_pipe{ctx};
  auto child = bp::process{
    ctx,
    self,
    {"--encro-crash-child"},
    bp::process_stdio{.out = childOut, .err = childErr}
  };
  child.wait();

  auto const output = readProcessStream(childOut) + readProcessStream(childErr);

  CHECK(child.exit_code() != 0);
  CHECK(output.find("[CRASH]") != std::string::npos);
  CHECK(output.find("stacktrace") != std::string::npos);
}

// ── RED 7.2 — NDJSON crash line construction ────────────────────────────────

TEST_CASE("formatCrashJsonLine builds a parseable NDJSON record", "[crash][run_id]") {
  auto const line =
    crash::formatCrashJsonLine("boom\nstack frame 1\nstack frame 2", "run-9");

  CHECK(line.ends_with('\n'));

  auto ec = boost::system::error_code{};
  auto const parsed = boost::json::parse(line, ec);
  REQUIRE_FALSE(ec);
  REQUIRE(parsed.is_object());

  auto const& obj = parsed.as_object();
  CHECK(obj.at("level").as_string() == "critical");
  CHECK(obj.at("module").as_string() == "infra.crash");
  CHECK(obj.at("run_id").as_string() == "run-9");
  // Multiline message round-trips through the escaping
  CHECK(obj.at("message").as_string() == "boom\nstack frame 1\nstack frame 2");
  // Timestamp matches the NDJSON schema (UTC, ms, Z)
  auto const ts = obj.at("timestamp").as_string();
  CHECK(ts.size() == 24);
  CHECK(ts[19] == '.');
  CHECK(ts.ends_with("Z"));
}

TEST_CASE("formatCrashJsonLine escapes quotes and backslashes", "[crash][run_id]") {
  auto const line =
    crash::formatCrashJsonLine(R"(say "hi" on C:\path\to\nowhere)", R"(run"id\1)");

  auto ec = boost::system::error_code{};
  auto const parsed = boost::json::parse(line, ec);
  REQUIRE_FALSE(ec);

  auto const& obj = parsed.as_object();
  CHECK(obj.at("message").as_string() == R"(say "hi" on C:\path\to\nowhere)");
  CHECK(obj.at("run_id").as_string() == R"(run"id\1)");
}

TEST_CASE("installHandlers is callable in-process and idempotent", "[crash]") {
  crash::installHandlers();
  crash::installHandlers();
  CHECK(true);
}

TEST_CASE(
  "crash report falls back to stderr when logging is not initialized",
  "[crash]"
) {
  // Ensure the async tier has no default logger to write to, forcing the
  // stderr tier (the tier the test process relies on: logging setup never
  // runs inside the test binary).
  auto guard = ScopedDefaultLogger(nullptr);
  TempDir temp;
  auto const errFile = temp.path / "err.txt";

  {
    auto capture = testutils::StderrCapture{errFile};
    auto const ex = std::runtime_error{"boom"};
    crash::reportCaughtException("unit-test", ex);
  }

  auto const content = testutils::readTextFile(errFile);
  CHECK(content.find("[CRASH]") != std::string::npos);
  CHECK(content.find("unit-test: boom") != std::string::npos);
  CHECK(content.find("stacktrace") != std::string::npos);
}
