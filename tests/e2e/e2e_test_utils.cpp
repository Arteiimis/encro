#include "e2e_test_utils.h"

#include "infra/env.h"

#include <boost/asio/buffer.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/process/v2/environment.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/start_dir.hpp>
#include <boost/process/v2/stdio.hpp>
#include <libzippp/libzippp.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <thread>

#if defined(_WIN32)
  #include <boost/process/v2/windows/creation_flags.hpp>
#else
  #include <csignal>
  #include <cstdlib>
#endif

namespace bp = boost::process::v2;

namespace {

auto platformBinaryName(std::string_view stem) -> std::string {
#if defined(_WIN32)
  return std::string{stem} + ".exe";
#else
  return std::string{stem};
#endif
}

auto executableDir() -> fs::path {
  auto const programPath = boost::dll::program_location();
  return fs::path{programPath.string()}.parent_path();
}

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

void setEnvVar(std::string const& key, std::optional<std::string> const& value) {
#if defined(_WIN32)
  ::SetEnvironmentVariableA(key.c_str(), value.has_value() ? value->c_str() : nullptr);
#else
  if (value.has_value()) {
    setenv(key.c_str(), value->c_str(), 1);
  } else {
    unsetenv(key.c_str());
  }
#endif
}

class ScopedEnvironmentOverrides {
public:
  explicit ScopedEnvironmentOverrides(
    std::map<std::string, std::string> const& overrides
  ) {
    originals_.reserve(overrides.size());
    for (auto const& [key, value]: overrides) {
      originals_.emplace_back(key, processenv::readEnvVar(key));
      setEnvVar(key, value);
    }
  }

  ~ScopedEnvironmentOverrides() {
    for (auto it = originals_.rbegin(); it != originals_.rend(); ++it) {
      setEnvVar(it->first, it->second);
    }
  }

private:
  std::vector<std::pair<std::string, std::optional<std::string>>> originals_;
};

auto runChild(
  fs::path const& executable,
  std::vector<std::string> const& args,
  std::optional<fs::path> const& workingDir
) -> e2e::ProcessResult {
  auto ctx = boost::asio::io_context{};
  auto childOut = boost::asio::readable_pipe{ctx};
  auto childErr = boost::asio::readable_pipe{ctx};
  auto const executableText = executable.string();
  auto stdoutText = std::string{};
  auto stderrText = std::string{};

  auto captureStreams = [&](auto& child) {
    auto stdoutReader = std::jthread([&] { stdoutText = readProcessStream(childOut); });
    auto stderrReader = std::jthread([&] { stderrText = readProcessStream(childErr); });
    child.wait();
    // Join before moving: the readers own the strings until pipe EOF, which
    // may arrive after the child exits (slow scheduling loses output otherwise).
    stdoutReader.join();
    stderrReader.join();
    return e2e::ProcessResult{
      child.exit_code(),
      std::move(stdoutText),
      std::move(stderrText)
    };
  };

  // Null stdin: prompts must not block on the test runner's own terminal.
  if (workingDir.has_value()) {
    auto child = bp::process{
      ctx,
      executableText,
      args,
      bp::process_stdio{.in = nullptr, .out = childOut, .err = childErr},
      bp::process_start_dir{workingDir->string()}
    };
    return captureStreams(child);
  }

  auto child = bp::process{
    ctx,
    executableText,
    args,
    bp::process_stdio{.in = nullptr, .out = childOut, .err = childErr}
  };
  return captureStreams(child);
}

void copyExecutableWithoutExtension(fs::path const& source, fs::path const& destination) {
  fs::create_directories(destination.parent_path());
  fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
}

}  // namespace

namespace e2e {

auto encroBinaryPath() -> fs::path {
  return executableDir() / platformBinaryName("encro");
}

auto fakeMediaToolBinaryPath() -> fs::path {
  return executableDir() / platformBinaryName("encro_e2e_tool");
}

auto resolveToolOnPath(std::string_view executable) -> std::optional<fs::path> {
  auto const resolved = bp::environment::find_executable(std::string{executable});
  if (resolved.empty()) { return std::nullopt; }
  return fs::path{resolved.string()};
}

auto runProcess(
  fs::path const& executable,
  std::vector<std::string> const& args,
  std::optional<fs::path> const& workingDir,
  std::map<std::string, std::string> const& environment
) -> ProcessResult {
  auto guard = ScopedEnvironmentOverrides{environment};
  return runChild(executable, args, workingDir);
}

auto runEncro(
  std::vector<std::string> const& args,
  std::optional<fs::path> const& workingDir,
  std::map<std::string, std::string> const& environment
) -> ProcessResult {
  return runProcess(encroBinaryPath(), args, workingDir, environment);
}

// The failure path prints "Log file: <path>"; dump that file's tail into the
// assertion output so CI runs carry ffmpeg's stderr without a separate
// artifact round-trip. Empty when the child produced no log line.
auto encroLogTail(std::string const& stdoutText) -> std::string {
  auto const marker = std::string_view{"Log file: "};
  auto const pos = stdoutText.find(marker);
  if (pos == std::string::npos) { return {}; }
  auto const eol = stdoutText.find('\n', pos);
  auto const path = stdoutText.substr(pos + marker.size(), eol - pos - marker.size());
  auto in = std::ifstream{fs::path{path}};
  if (!in) { return "\n(encro log not readable: " + path + ")\n"; }
  auto content = std::string{};
  auto line = std::string{};
  while (std::getline(in, line)) {
    content += line;
    content += '\n';
  }
  if (content.size() > 8192) { content.erase(0, content.size() - 8192); }
  return "\nencro log (" + path + "):\n" + content;
}

namespace {

// Creates the child as a process-group leader on Windows (POSIX needs no
// group: kill targets a single pid).
auto makeChild(
  boost::asio::io_context& ctx,
  fs::path const& executable,
  std::vector<std::string> const& args,
  std::optional<fs::path> const& workingDir,
  boost::asio::readable_pipe& stdoutStream,
  boost::asio::readable_pipe& stderrStream
) -> bp::process {
  auto const executableText = executable.string();
#if defined(_WIN32)
  if (workingDir.has_value()) {
    return bp::process{
      ctx,
      executableText,
      args,
      bp::process_stdio{.in = nullptr, .out = stdoutStream, .err = stderrStream},
      bp::process_start_dir{workingDir->string()},
      bp::windows::create_new_process_group
    };
  }
  return bp::process{
    ctx,
    executableText,
    args,
    bp::process_stdio{.in = nullptr, .out = stdoutStream, .err = stderrStream},
    bp::windows::create_new_process_group
  };
#else
  if (workingDir.has_value()) {
    return bp::process{
      ctx,
      executableText,
      args,
      bp::process_stdio{.in = nullptr, .out = stdoutStream, .err = stderrStream},
      bp::process_start_dir{workingDir->string()}
    };
  }
  return bp::process{
    ctx,
    executableText,
    args,
    bp::process_stdio{.in = nullptr, .out = stdoutStream, .err = stderrStream}
  };
#endif
}

}  // namespace

RunningProcess::RunningProcess(
  fs::path const& executable,
  std::vector<std::string> const& args,
  std::optional<fs::path> const& workingDir
)
  : child_(makeChild(ctx_, executable, args, workingDir, stdoutStream_, stderrStream_)) {
  stdoutReader_ =
    std::jthread([this] { stdoutText_ = readProcessStream(stdoutStream_); });
  stderrReader_ =
    std::jthread([this] { stderrText_ = readProcessStream(stderrStream_); });
}

RunningProcess::~RunningProcess() {
  // Terminate before joining the readers: they block until pipe EOF, and the
  // child keeps its write ends open while running.
  boost::system::error_code ec;
  if (child_.running(ec) && !ec) { child_.terminate(ec); }
  if (stdoutReader_.joinable()) { stdoutReader_.join(); }
  if (stderrReader_.joinable()) { stderrReader_.join(); }
}

auto RunningProcess::wait(std::chrono::milliseconds timeout)
  -> std::optional<ProcessResult> {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (child_.running() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
  }
  if (child_.running()) { return std::nullopt; }

  child_.wait();
  stdoutReader_.join();
  stderrReader_.join();

  return ProcessResult{
    child_.exit_code(),
    std::move(stdoutText_),
    std::move(stderrText_)
  };
}

bool RunningProcess::sendCtrlC() {
#if defined(_WIN32)
  // CTRL_BREAK_EVENT, not CTRL_C_EVENT: under ConPTY/mintty the C event is
  // silently dropped for CREATE_NEW_PROCESS_GROUP children, while BREAK is
  // always delivered. encro's console handler treats both identically.
  return ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, static_cast<DWORD>(child_.id()))
    != FALSE;
#else
  return ::kill(child_.id(), SIGINT) == 0;
#endif
}

void RunningProcess::terminate() {
  child_.terminate();
}

std::size_t RunningProcess::id() {
  return static_cast<std::size_t>(child_.id());
}

auto runEncroAsync(
  std::vector<std::string> const& args,
  std::optional<fs::path> const& workingDir,
  std::map<std::string, std::string> const& environment
) -> RunningProcess {
  auto guard = ScopedEnvironmentOverrides{environment};
  return RunningProcess{encroBinaryPath(), args, workingDir};
}

bool consoleCtrlEventsAvailable() {
#if defined(_WIN32)
  // GetConsoleWindow() is NULL under ConPTY even though a console exists;
  // GetConsoleCP() is nonzero exactly when the process has a console.
  return ::GetConsoleCP() != 0;
#else
  return true;
#endif
}

auto installFakeToolchain(fs::path const& root) -> FakeToolchain {
  auto const toolBinary = fakeMediaToolBinaryPath();
  auto const binDir = root / "bin";
  auto const ffmpegPath = binDir / "ffmpeg";
  auto const ffprobePath = binDir / "ffprobe";

  copyExecutableWithoutExtension(toolBinary, ffmpegPath);
  copyExecutableWithoutExtension(toolBinary, ffprobePath);

  return {
    .root = root,
    .ffmpegPath = ffmpegPath,
    .ffprobePath = ffprobePath,
  };
}

void writeTextFile(fs::path const& path, std::string_view content) {
  fs::create_directories(path.parent_path());
  auto out = std::ofstream{path, std::ios::binary};
  out << content;
}

auto listZipEntries(fs::path const& zipPath) -> std::vector<std::string> {
  auto zip = libzippp::ZipArchive{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);

  auto entries = std::vector<std::string>{};
  for (auto const& entry: zip.getEntries()) {
    if (entry.getName().ends_with('/')) { continue; }
    entries.emplace_back(entry.getName());
  }

  std::ranges::sort(entries);
  zip.close();
  return entries;
}

auto mapZipEntryCompression(fs::path const& zipPath)
  -> std::map<std::string, libzippp::CompressionMethod> {
  auto zip = libzippp::ZipArchive{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);

  auto methods = std::map<std::string, libzippp::CompressionMethod>{};
  for (auto const& entry: zip.getEntries()) {
    if (entry.getName().ends_with('/')) { continue; }
    methods.emplace(entry.getName(), entry.getCompressionMethod());
  }
  zip.close();
  return methods;
}

}  // namespace e2e
