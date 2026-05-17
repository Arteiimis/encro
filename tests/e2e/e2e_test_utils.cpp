#include "e2e_test_utils.h"

#include "infra/env.h"

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/process/v1.hpp>
#include <libzippp/libzippp.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <thread>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <cstdlib>
#endif

namespace bp = boost::process::v1;

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

auto readProcessStream(bp::ipstream& stream) -> std::string {
  return std::string{std::istreambuf_iterator<char>{stream}, {}};
}

auto setEnvVar(std::string const& key, std::optional<std::string> const& value) -> void {
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
  auto childOut = bp::ipstream{};
  auto childErr = bp::ipstream{};
  auto const executableText = executable.string();
  auto stdoutText = std::string{};
  auto stderrText = std::string{};

  auto captureStreams = [&](auto& child) {
    auto stdoutReader = std::jthread([&] { stdoutText = readProcessStream(childOut); });
    auto stderrReader = std::jthread([&] { stderrText = readProcessStream(childErr); });
    child.wait();
    return e2e::ProcessResult{
      child.exit_code(),
      std::move(stdoutText),
      std::move(stderrText)
    };
  };

  if (workingDir.has_value()) {
    auto child = bp::child(
      executableText,
      bp::args(args),
      bp::start_dir = workingDir->string(),
      bp::std_out > childOut,
      bp::std_err > childErr
    );
    return captureStreams(child);
  }

  auto child = bp::child(
    executableText,
    bp::args(args),
    bp::std_out > childOut,
    bp::std_err > childErr
  );
  return captureStreams(child);
}

auto copyExecutableWithoutExtension(fs::path const& source, fs::path const& destination)
  -> void {
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
  auto const resolved = bp::search_path(std::string{executable});
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

auto writeTextFile(fs::path const& path, std::string_view content) -> void {
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

}  // namespace e2e
