#include "core/crash_runtime.h"
#include "test_utils.h"

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/process/v1.hpp>
#include <catch2/catch_all.hpp>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
namespace bp = boost::process::v1;

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

auto readProcessStream(bp::ipstream& stream) -> std::string {
  return std::string{std::istreambuf_iterator<char>{stream}, {}};
}

}  // namespace

TEST_CASE("reportCaughtException writes crash report to default logger", "[crash]") {
  TempDir temp;
  auto const logPath = temp.path / "crash.log";

  auto sink =
    std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
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

  auto sink =
    std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
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

  auto childOut = bp::ipstream{};
  auto childErr = bp::ipstream{};
  auto child =
    bp::child(self, "[crash-child]", bp::std_out > childOut, bp::std_err > childErr);
  child.wait();

  auto const output = readProcessStream(childOut) + readProcessStream(childErr);

  CHECK(child.exit_code() != 0);
  CHECK(output.find("[CRASH]") != std::string::npos);
  CHECK(output.find("stacktrace") != std::string::npos);
}

TEST_CASE("child process intentionally crashes", "[crash-child][.]") {
  crash::installHandlers();

#if defined(_WIN32)
  auto* ptr = static_cast<volatile int*>(nullptr);
  *ptr = 7;
#else
  std::raise(SIGABRT);
#endif

  FAIL("child process should have crashed before reaching this line");
}
