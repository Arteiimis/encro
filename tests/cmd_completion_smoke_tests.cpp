// Real-shell smoke tests: source the generated scripts under actual
// bash/powershell and drive candidate output end to end. Opt-in via
// ENCRO_TEST_COMPLETION (see requireCompletionSmokeOrSkip); also skipped when
// the shell is not on PATH.
#include "infra/env.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
  #include <windows.h>
#endif

#if defined(_WIN32)
auto popenText(char const* commandLine) -> FILE* {
  return _popen(commandLine, "r");
}
int pcloseText(FILE* stream) {
  return _pclose(stream);
}
#else
auto popenText(char const* commandLine) -> FILE* {
  return popen(commandLine, "r");
}
int pcloseText(FILE* stream) {
  return pclose(stream);
}
#endif

namespace fs = std::filesystem;

namespace {

auto executableDir() -> fs::path {
#if defined(_WIN32)
  auto buffer = std::array<char, MAX_PATH>{};
  GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  return fs::path{buffer.data()}.parent_path();
#else
  return fs::read_symlink("/proc/self/exe").parent_path();
#endif
}

auto encroBinary() -> fs::path {
#if defined(_WIN32)
  return executableDir() / "encro.exe";
#else
  return executableDir() / "encro";
#endif
}

auto forwardSlashes(std::string path) -> std::string {
  std::ranges::replace(path, '\\', '/');
  return path;
}

// Runs a command line, returns (exitCode, stdout). cmd/POSIX differences are
// hidden by the C runtime's popen/system.
auto runCapture(std::string const& commandLine) -> std::pair<int, std::string> {
  auto stream = popenText(commandLine.c_str());
  REQUIRE(stream != nullptr);
  auto output = std::string{};
  std::array<char, 4096> buffer{};
  while (auto const got = fread(buffer.data(), 1, buffer.size(), stream)) {
    output.append(buffer.data(), got);
  }
  auto const exitCode = pcloseText(stream);
  return {exitCode, output};
}

auto shellAvailable(std::string const& probeCommandLine) -> bool {
  auto stream = popenText(probeCommandLine.c_str());
  if (stream == nullptr) { return false; }
  std::array<char, 256> buffer{};
  while (fread(buffer.data(), 1, buffer.size(), stream) > 0) { }
  return pcloseText(stream) == 0;
}

auto emitScriptTo(TempDir const& temp, std::string const& shell, std::string const& name)
  -> fs::path {
  auto const command =
    "\"" + forwardSlashes(encroBinary().string()) + "\" completion " + shell;
  auto const [exitCode, output] = runCapture(command);
  REQUIRE(exitCode == 0);
  auto const path = temp.path / name;
  auto stream = std::ofstream{path, std::ios::binary | std::ios::trunc};
  stream << output;
  REQUIRE(stream.good());
  return path;
}

// Real-shell coverage spawns bash/PowerShell processes per probe and is
// exercised rarely, so it stays out of default runs; opt in when touching the
// completion scripts:   ENCRO_TEST_COMPLETION=1 xmake test-report --tag="[smoke]"
void requireCompletionSmokeOrSkip() {
  if (processenv::readNonEmptyEnvVar("ENCRO_TEST_COMPLETION").has_value()) { return; }
  SKIP("Completion smoke coverage is opt-in; set ENCRO_TEST_COMPLETION=1 to run it.");
}

}  // namespace

TEST_CASE("bash smoke: sourced script completes candidates", "[completion][smoke]") {
  requireCompletionSmokeOrSkip();
  if (!shellAvailable("bash -c exit")) { SKIP("bash not available on PATH."); }

  TempDir temp;
  auto const scriptPath = emitScriptTo(temp, "bash", "encro-bash.sh");

  // Driver: source the completion, drive several positions, print candidates.
  auto const driver = temp.path / "drive.sh";
  {
    auto stream = std::ofstream{driver, std::ios::binary | std::ios::trunc};
    REQUIRE(stream.is_open());
    stream
      << "set -e\n"
      << "source \""
      << forwardSlashes(scriptPath.string())
      << "\"\n"
      << "probe() {\n"
      << "  COMP_WORDS=(\"$@\")\n"
      << "  COMP_CWORD=$(( ${#COMP_WORDS[@]} - 1 ))\n"
      << "  COMPREPLY=()\n"
      << "  _encro_complete\n"
      << "  echo \"${COMPREPLY[*]}\"\n"
      << "}\n"
      << "echo \"ENUM: $(probe encro --output-format '')\"\n"
      << "echo \"HIDDEN: $(probe encro --resume --re)\"\n"
      << "echo \"SUB: $(probe encro pre)\"\n"
      << "echo \"CFGVAL: $(probe encro config --set output-format '')\"\n"
      << "echo \"SETDONE: $(probe encro config --set jobs 4 --)\"\n";
  }

  auto const [exitCode, output] =
    runCapture("bash \"" + forwardSlashes(driver.string()) + "\"");
  REQUIRE(exitCode == 0);
  CHECK(output.find("ENUM: mp4 webp") != std::string::npos);
  // --restart is excluded by the typed --resume; --recursive still offered.
  CHECK(output.find("HIDDEN: --recursive --resume") != std::string::npos);
  CHECK(output.find("HIDDEN: --restart") == std::string::npos);
  CHECK(output.find("SUB: preview") != std::string::npos);
  CHECK(output.find("CFGVAL: mp4 webp") != std::string::npos);
  // after a completed --set pair, remaining config options are offered and
  // the four other actions are excluded
  CHECK(output.find("SETDONE: --help --set") != std::string::npos);
  CHECK(output.find("--get") == std::string::npos);
}

TEST_CASE("powershell smoke: TabExpansion2 returns candidates", "[completion][smoke]") {
  requireCompletionSmokeOrSkip();
  auto shell = std::optional<std::string>{};
  for (auto const* candidate: {"pwsh", "powershell"}) {
    if (shellAvailable(std::string{candidate} + " -NoProfile -Command exit")) {
      shell = candidate;
      break;
    }
  }
  if (!shell.has_value()) { SKIP("powershell not available on PATH."); }

  TempDir temp;
  auto const scriptPath = emitScriptTo(temp, "powershell", "encro-pwsh.ps1");
  auto const scriptRef = "\"" + forwardSlashes(scriptPath.string()) + "\"";

  auto const driver = temp.path / "drive-ps.ps1";
  {
    auto stream = std::ofstream{driver, std::ios::binary | std::ios::trunc};
    REQUIRE(stream.is_open());
    stream
      << ". "
      << scriptRef
      << "\n"
      << "function Probe([string] $line) {\n"
      << "  $r = TabExpansion2 -inputScript $line -cursorColumn $line.Length\n"
      << "  (@($r.CompletionMatches) | ForEach-Object { $_.CompletionText }) -join ','\n"
      << "}\n"
      << "echo \"ENUM: $(Probe 'encro --output-format ')\"\n"
      << "echo \"HIDDEN: $(Probe 'encro --resume --re')\"\n"
      << "echo \"SUB: $(Probe 'encro pre')\"\n"
      << "echo \"CFGVAL: $(Probe 'encro config --set output-format ')\"\n"
      << "echo \"SCOPE: $(Probe 'encro preview --')\"\n"
      << "echo \"SETDONE: $(Probe 'encro config --set jobs 4 --')\"\n";
  }

  auto const [exitCode, output] = runCapture(
    *shell
    + " -NoProfile -ExecutionPolicy Bypass -File \""
    + forwardSlashes(driver.string())
    + "\""
  );
  REQUIRE(exitCode == 0);
  CHECK(output.find("ENUM: mp4,webp") != std::string::npos);
  CHECK(output.find("HIDDEN: --recursive,--resume") != std::string::npos);
  CHECK(output.find("HIDDEN: --restart") == std::string::npos);
  CHECK(output.find("SUB: preview") != std::string::npos);
  CHECK(output.find("CFGVAL: mp4,webp") != std::string::npos);
  // preview scope: its own options only, no main-command --pack
  CHECK(output.find("--pack") == std::string::npos);
  CHECK(output.find("--start") != std::string::npos);
  CHECK(output.find("SETDONE: --help,--set") != std::string::npos);
}
