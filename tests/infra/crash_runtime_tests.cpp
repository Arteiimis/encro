#include "infra/crash_runtime.h"
#include "test_utils.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>
#include <catch2/catch_all.hpp>
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
