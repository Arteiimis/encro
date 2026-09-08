#include "infra/crash_runtime.h"

#include "logging/log_tags.h"
#include "logging/logging.h"
#include "logging/setup.h"
#include "test_utils.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/json.hpp>  // IWYU pragma: keep
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
  #include <windows.h>
#endif

namespace fs = std::filesystem;
namespace bp = boost::process::v2;

DEFINE_LOGGER(logtags::TEST_INFRA);

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

struct CrashChild {
  int exitCode = 0;
  std::string output;
};

// Spawns this test binary bounded: waits at most `bound` for exit and kills
// the child on expiry, so a deadlocked child fails fast instead of hanging
// the suite. Captures merged stdout+stderr once the child is gone.
auto spawnSelfBounded(std::vector<std::string> const& args, std::chrono::seconds bound)
  -> CrashChild {
  auto ctx = boost::asio::io_context{};
  auto childOut = boost::asio::readable_pipe{ctx};
  auto childErr = boost::asio::readable_pipe{ctx};
  auto child = bp::process{
    ctx,
    boost::dll::program_location(),
    args,
    bp::process_stdio{.out = childOut, .err = childErr}
  };
  // std::async's future blocks in its destructor, so the kill branch still
  // waits for the (terminated) child before returning.
  auto waiter = std::async(std::launch::async, [&] { return child.wait(); });
  if (waiter.wait_for(bound) != std::future_status::ready) {
    child.terminate();
    return {-1, "<spawn timed out and was killed>"};
  }
  return {waiter.get(), readProcessStream(childOut) + readProcessStream(childErr)};
}

// Spawns this test binary with the given argument list and captures its exit
// code and merged stdout+stderr; bounded far above any legit crash-child
// runtime so only a deadlocked child trips it.
auto spawnSelf(std::vector<std::string> const& args) -> CrashChild {
  return spawnSelfBounded(args, std::chrono::seconds{120});
}

#if defined(_WIN32)
// Raises a fatal-code exception that local SEH immediately catches; the
// first-chance VEH sees it before the __except filter. Kept free of C++
// objects with destructors so it can host __try.
bool raiseAndCatchFatalCode() {
  auto caught = false;
  __try {
    ::RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
  } __except (caught = true, EXCEPTION_EXECUTE_HANDLER) { }
  return caught;
}
#endif

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

TEST_CASE("crash context provider annotates crash records", "[crash]") {
  TempDir temp;
  auto const logPath = temp.path / "context.log";

  auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
  auto logger = std::make_shared<spdlog::logger>("crash-context", sink);
  logger->set_level(spdlog::level::trace);

  auto guard = ScopedDefaultLogger(logger);
  crash::setCrashContextProvider([]() -> std::string { return "the-running-test"; });
  auto const ex = std::runtime_error{"boom"};
  crash::reportCaughtException("unit-test", ex);
  logger->flush();

  auto const content = readText(logPath);
  CHECK(content.find("[context: the-running-test]") != std::string::npos);

  // Clearing the provider stops the annotation (no placeholder noise).
  crash::setCrashContextProvider(nullptr);
  crash::reportCaughtException("unit-test", ex);
  logger->flush();

  auto const cleared = readText(logPath);
  auto const firstRecord = cleared.find("unit-test: boom");
  auto const secondRecord = cleared.find("unit-test: boom", firstRecord + 1);
  REQUIRE(firstRecord != std::string::npos);
  REQUIRE(secondRecord != std::string::npos);
  CHECK(cleared.find("[context:", secondRecord) == std::string::npos);
}

TEST_CASE("crash runtime handles real process crash", "[crash][integration]") {
  auto const child = spawnSelf({"--encro-crash-child"});

  CHECK(child.exitCode != 0);
  CHECK(child.output.find("[CRASH]") != std::string::npos);
  CHECK(child.output.find("stacktrace") != std::string::npos);
}

// ── Test-binary startup: log routing ────────────────────────────────────────

// Selector for the hidden probe below; the meta-check spawns this binary with
// the same tag.
constexpr auto kLogProbeTag = "[.][log-probe]";

// Hidden probe: the child must start with a clean logger registry, so an
// in-process probe would read state other test cases left behind instead.
TEST_CASE("log probe", kLogProbeTag) {
  LOG_INFO("log-probe-marker");
}

TEST_CASE("test binary keeps log records off its output streams", "[test-utils][meta]") {
  // A fresh process starts with exactly the logger registry test_main
  // installs; a fallback LOG_INFO record must reach neither stream.
  auto const child = spawnSelf({kLogProbeTag});
  CHECK(child.exitCode == 0);
  CHECK(child.output.find("log-probe-marker") == std::string::npos);
}

TEST_CASE(
  "hardening violation produces a single crash record",
  "[crash][integration][hardening]"
) {
  auto const child = spawnSelf({"--encro-crash-child=oob"});

  CHECK(child.exitCode != 0);
  CHECK(child.output.find("[CRASH]") != std::string::npos);
  CHECK(child.output.find("stacktrace") != std::string::npos);
  // Context provider annotation survives the whole crash path.
  CHECK(child.output.find("[context: crash-child-oob]") != std::string::npos);
#if defined(_WIN32)
  CHECK(child.output.find("0xC000001D") != std::string::npos);
  // Release PDBs resolve the application frames (module!function), so the
  // stack is actionable instead of bare offsets.
  CHECK(child.output.find("tests!") != std::string::npos);
  // The first-chance handler reports once; the UE filter suppresses the
  // duplicate for the same exception.
  CHECK(
    child.output.find("stacktrace", child.output.find("stacktrace") + 1)
    == std::string::npos
  );
#endif
}

TEST_CASE(
  "in-session hardening violation still produces a crash record",
  "[crash][integration][hardening]"
) {
  // The child runs a real Catch2 session: its FatalConditionHandler owns the
  // UE filter slot while the gated test case traps, so this pins the scenario
  // the filter displacement used to break.
  auto gate = testutils::ScopedEnvVar{"ENCRO_TEST_CRASH_OOB", "1"};
  auto const child = spawnSelf({"[crash-on-demand]"});

  CHECK(child.exitCode != 0);
  CHECK(child.output.find("[CRASH]") != std::string::npos);
  CHECK(child.output.find("stacktrace") != std::string::npos);
  // Real provider (installed in the test runner's main): the record names the
  // crashing test.
  CHECK(child.output.find("[context: in-session hardening crash]") != std::string::npos);
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

TEST_CASE("crash reason reaches stderr when the log file is writable", "[crash]") {
  TempDir temp;
  auto config = logging::LogConfig{
    .colorsEnabled = false,
    .customLogDir = temp.path,
  };
  auto const setupResult = logging::setup(config);
  REQUIRE(setupResult.has_value());
  auto const logPath = logging::currentLogFilePath();
  REQUIRE(logPath.has_value());

  auto const errFile = temp.path / "err.txt";
  {
    auto capture = testutils::StderrCapture{errFile};
    auto const ex = std::runtime_error{"boom"};
    crash::reportCaughtException("unit-test", ex);
  }

  // One line on stderr: reason plus the log path, no stacktrace.
  auto const errText = testutils::readTextFile(errFile);
  CHECK(errText.find("[CRASH] unit-test: boom") != std::string::npos);
  CHECK(errText.find("[log:") != std::string::npos);
  CHECK(errText.find(logPath.value().string()) != std::string::npos);
  CHECK(errText.find("stacktrace") == std::string::npos);
  // The full report stays in the log file.
  auto const logText = testutils::readTextFile(logPath.value());
  CHECK(logText.find("unit-test: boom") != std::string::npos);
  CHECK(logText.find("stacktrace") != std::string::npos);

  logging::shutdown();
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

// ── DLL-load zone: crash paths stand down under loader lock ──────────────────

TEST_CASE("DLL-load zone is scoped, nestable, and thread-local", "[crash]") {
  CHECK_FALSE(crash::inDllLoadZone());
  {
    auto const outer = crash::ScopedDllLoadZone{};
    CHECK(crash::inDllLoadZone());
    {
      auto const inner = crash::ScopedDllLoadZone{};
      CHECK(crash::inDllLoadZone());
    }
    CHECK(crash::inDllLoadZone());
  }
  CHECK_FALSE(crash::inDllLoadZone());

  // Cross-thread: a zone held on another thread leaves this one unaffected.
  auto zoneEntered = std::atomic<bool>{false};
  auto release = std::atomic<bool>{false};
  auto holder = std::thread{[&]() {
    auto const zone = crash::ScopedDllLoadZone{};
    zoneEntered.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) { std::this_thread::yield(); }
  }};
  REQUIRE(testutils::waitUntil([&] { return zoneEntered.load(); }));
  CHECK_FALSE(crash::inDllLoadZone());
  release.store(true, std::memory_order_release);
  holder.join();
  CHECK_FALSE(crash::inDllLoadZone());
}

TEST_CASE("crash reports inside a DLL-load zone omit the stacktrace", "[crash]") {
  TempDir temp;
  auto const logPath = temp.path / "zoned.log";

  auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
  auto logger = std::make_shared<spdlog::logger>("crash-zoned", sink);
  logger->set_level(spdlog::level::trace);

  auto guard = ScopedDefaultLogger(logger);
  auto const zone = crash::ScopedDllLoadZone{};
  auto const ex = std::runtime_error{"boom"};
  crash::reportCaughtException("unit-test", ex);
  logger->flush();

  auto const content = readText(logPath);
  CHECK(content.find("[CRASH]") != std::string::npos);
  CHECK(content.find("unit-test: boom") != std::string::npos);
  // The omission marker replaces the trace; no frames leak through.
  CHECK(content.find("<skipped: DLL-load zone (loader lock)>") != std::string::npos);
  CHECK(content.find("#00") == std::string::npos);
}

#if defined(_WIN32)
TEST_CASE(
  "fatal-code exceptions pass through the VEH inside a DLL-load zone",
  "[crash]"
) {
  TempDir temp;
  auto const logPath = temp.path / "pass-through.log";

  auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
  auto logger = std::make_shared<spdlog::logger>("crash-pass-through", sink);
  logger->set_level(spdlog::level::trace);

  auto guard = ScopedDefaultLogger(logger);
  {
    auto const zone = crash::ScopedDllLoadZone{};
    REQUIRE(raiseAndCatchFatalCode());
  }
  logger->flush();

  // The exception was a handled probe from the VEH's point of view: no
  // record may exist for it anywhere.
  auto const content = readText(logPath);
  CHECK(content.find("[CRASH]") == std::string::npos);
}

TEST_CASE(
  "fatal-code exceptions outside a DLL-load zone still report first-chance",
  "[crash]"
) {
  TempDir temp;
  auto const logPath = temp.path / "control.log";

  auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
  auto logger = std::make_shared<spdlog::logger>("crash-control", sink);
  logger->set_level(spdlog::level::trace);

  auto guard = ScopedDefaultLogger(logger);
  REQUIRE(raiseAndCatchFatalCode());
  logger->flush();

  // Control: the same raise without the zone produces the first-chance
  // record (reason + stacktrace) as before the carve-out.
  auto const content = readText(logPath);
  CHECK(content.find("[CRASH]") != std::string::npos);
  CHECK(content.find("0xC0000005") != std::string::npos);
  CHECK(content.find("stacktrace") != std::string::npos);
}
#endif

TEST_CASE(
  "dll-zone crash-child exits promptly without a crash record",
  "[crash][integration]"
) {
  // Bounded wait: a regression deadlocks the child exactly like the backlog
  // hang, so the spawn must fail fast (killed at the bound) instead of
  // hanging the suite.
  auto const child =
    spawnSelfBounded({"--encro-crash-child=dll-zone"}, std::chrono::seconds{10});

  REQUIRE(child.exitCode == 0);
#if defined(_WIN32)
  CHECK(child.output.find("dll-zone-child caught=1") != std::string::npos);
#endif
  CHECK(child.output.find("[CRASH]") == std::string::npos);
}
