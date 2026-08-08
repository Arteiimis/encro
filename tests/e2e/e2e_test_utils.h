#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/process/v2/process.hpp>

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace e2e {

struct ProcessResult {
  int exitCode = -1;
  std::string stdoutText;
  std::string stderrText;
};

class RunningProcess {
public:
  explicit RunningProcess(
    fs::path const& executable,
    std::vector<std::string> const& args,
    std::optional<fs::path> const& workingDir = std::nullopt
  );

  ~RunningProcess();

  // Non-copyable and non-movable: asio::io_context is immovable.
  // runEncroAsync relies on guaranteed copy elision for its return value.
  RunningProcess(RunningProcess const&) = delete;
  RunningProcess& operator=(RunningProcess const&) = delete;

  // Waits up to timeout; nullopt means the process is still running.
  auto wait(std::chrono::milliseconds timeout) -> std::optional<ProcessResult>;

  // Delivers Ctrl+C to the process group (Windows: console event; POSIX: SIGINT).
  auto sendCtrlC() -> bool;

  auto terminate() -> void;

  auto id() -> std::size_t;

private:
  // Order matters: the context must outlive the pipes; streams must outlive
  // the child; the child must be terminated before the readers join (they
  // block on pipe EOF).
  boost::asio::io_context ctx_;
  boost::asio::readable_pipe stdoutStream_{ctx_};
  boost::asio::readable_pipe stderrStream_{ctx_};
  boost::process::v2::process child_;
  std::jthread stdoutReader_;
  std::jthread stderrReader_;
  std::string stdoutText_;
  std::string stderrText_;
};

struct FakeToolchain {
  fs::path root;
  fs::path ffmpegPath;
  fs::path ffprobePath;
};

auto encroBinaryPath() -> fs::path;

auto fakeMediaToolBinaryPath() -> fs::path;

auto resolveToolOnPath(std::string_view executable) -> std::optional<fs::path>;

auto runProcess(
  fs::path const& executable,
  std::vector<std::string> const& args,
  std::optional<fs::path> const& workingDir = std::nullopt,
  std::map<std::string, std::string> const& environment = {}
) -> ProcessResult;

auto runEncro(
  std::vector<std::string> const& args,
  std::optional<fs::path> const& workingDir = std::nullopt,
  std::map<std::string, std::string> const& environment = {}
) -> ProcessResult;

// Same as runEncro but returns a live handle for interruption tests.
// The child is created in its own process group so console events can be
// delivered to it without signalling the test runner.
auto runEncroAsync(
  std::vector<std::string> const& args,
  std::optional<fs::path> const& workingDir = std::nullopt,
  std::map<std::string, std::string> const& environment = {}
) -> RunningProcess;

// False when the platform cannot deliver console events to a child
// (Windows without a console); interruption tests SKIP in that case.
auto consoleCtrlEventsAvailable() -> bool;

auto installFakeToolchain(fs::path const& root) -> FakeToolchain;

auto writeTextFile(fs::path const& path, std::string_view content = "x") -> void;

auto listZipEntries(fs::path const& zipPath) -> std::vector<std::string>;

}  // namespace e2e
