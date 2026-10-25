// Real-shell smoke tests: source the generated scripts under actual
// bash/powershell and drive candidate output end to end. Opt-in via
// ENCRO_TEST_COMPLETION (see requireCompletionSmokeOrSkip); also skipped when
// the shell is not on PATH.
#include "core/path_utf8.h"
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

bool shellAvailable(std::string const& probeCommandLine) {
  auto stream = popenText(probeCommandLine.c_str());
  if (stream == nullptr) { return false; }
  std::array<char, 256> buffer{};
  while (fread(buffer.data(), 1, buffer.size(), stream) > 0) { }
  return pcloseText(stream) == 0;
}

auto emitScriptTo(TempDir const& temp, std::string const& shell, std::string const& name)
  -> fs::path {
  auto const command =
    "\"" + forwardSlashes(pathutf8::fromPath(encroBinary())) + "\" completion " + shell;
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

// The line a driver probe printed, so an assertion can check a whole candidate
// set instead of a substring. Missing marker yields an empty string.
auto lineStartingWith(std::string const& text, std::string const& marker) -> std::string {
  auto const from = text.find(marker);
  if (from == std::string::npos) { return {}; }
  auto const to = text.find('\n', from);
  return text.substr(from, to == std::string::npos ? std::string::npos : to - from);
}

// Seeds a template store through the real subcommand, so the dynamic slots have
// names to complete. The caller holds the ENCRO_CONFIG override.
bool seedTemplateStore(fs::path const& storeDir, std::string const& args) {
  std::error_code ec;
  fs::create_directories(storeDir, ec);
  auto const command =
    "\"" + forwardSlashes(pathutf8::fromPath(encroBinary())) + "\" " + args;
  return runCapture(command).first == 0;
}

}  // namespace

TEST_CASE("bash smoke: sourced script completes candidates", "[completion][smoke]") {
  requireCompletionSmokeOrSkip();
  if (!shellAvailable("bash -c exit")) { SKIP("bash not available on PATH."); }

  // organize is only offered with a backend; the path-delegation probe below
  // needs its scope in the emitted script.
  auto const fake = testutils::ScopedEnvVar{"ENCRO_FAKE_TAGGER", "fixture.json"};

  TempDir temp;
  auto const scriptPath = emitScriptTo(temp, "bash", "encro-bash.sh");
  REQUIRE(testutils::readTextFile(scriptPath).find("organize") != std::string::npos);

  // The dynamic slots read the live template store through the binary on PATH,
  // so the store is seeded first and the working directory gains a marker
  // `.json` file: a broken file-fallback suppression would surface it.
  auto const storeDir = temp.path / "store";
  auto const cwd = temp.path / "cwd";
  auto const emptyConfig = temp.path / "empty" / "config.json";
  std::error_code ec;
  fs::create_directories(cwd, ec);
  REQUIRE(fs::exists(cwd));
  { auto marker = std::ofstream{cwd / "marker.json", std::ios::binary}; }
  auto const configOverride =
    testutils::ScopedEnvVar{"ENCRO_CONFIG", pathutf8::fromPath(storeDir / "config.json")};
  REQUIRE(seedTemplateStore(storeDir, "template save anime --crf 24"));
  REQUIRE(seedTemplateStore(storeDir, "template save webp --preset p5"));

  // Driver: source the completion, drive several positions, print candidates.
  auto const driver = temp.path / "drive.sh";
  {
    auto stream = std::ofstream{driver, std::ios::binary | std::ios::trunc};
    REQUIRE(stream.is_open());
    stream
      << "set -e\n"
      << "source \""
      << forwardSlashes(pathutf8::fromPath(scriptPath))
      << "\"\n"
      // Shadow the compopt builtin: _encro_no_candidates becomes observable
      // as a SUPPRESSED marker in the probe output (fall-through prints none).
      << "compopt() { echo SUPPRESSED; }\n"
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
      << "echo \"CFGVERB: $(probe encro config '')\"\n"
      << "echo \"CFGKEY: $(probe encro config set '')\"\n"
      << "echo \"CFGVAL: $(probe encro config set output-format '')\"\n"
      << "echo \"SETDONE: $(probe encro config set jobs 4 --)\"\n"
      << "echo \"POSBARE: $(probe encro completion '')\"\n"
      << "echo \"POSPREFIX: $(probe encro completion p)\"\n"
      << "echo \"POSFLAG: $(probe encro completion --install '')\"\n"
      << "echo \"POSDONE: $(probe encro completion bash '')\"\n"
      << "echo \"POSNOMATCH: $(probe encro completion zz)\"\n"
      << "echo \"POSPATH: $(probe encro organize --model-dir X '')\"\n"
      // Dynamic template-name slots: the glue runs `encro completion
      // --candidates`, so the store probes need the build dir on PATH.
      << "export PATH=\"$(cygpath -u '"
      << forwardSlashes(pathutf8::fromPath(executableDir()))
      << "'):$PATH\"\n"
      << "cd \""
      << forwardSlashes(pathutf8::fromPath(cwd))
      << "\"\n"
      << "echo \"TNAME: $(probe encro -T '')\"\n"
      << "echo \"TNAMEPREFIX: $(probe encro -T a)\"\n"
      << "echo \"TEMPLONGFORM: $(probe encro --template w)\"\n"
      << "echo \"SHOWNAME: $(probe encro template show '')\"\n"
      << "echo \"DELPREFIX: $(probe encro template delete a)\"\n"
      << "echo \"KEYS: $(probe encro template unset anime '')\"\n"
      << "echo \"KEYPREFIX: $(probe encro template unset anime cr)\"\n"
      << "echo \"UNSETSLOT: $(probe encro template unset '')\"\n"
      << "echo \"LISTSLOT: $(probe encro template list '')\"\n"
      << "echo \"TSECOND: $(probe encro -T anime -T '')\"\n"
      << "echo \"TPLVERB: $(probe encro template '')\"\n"
      << "echo \"TPLSTATIC: $(probe encro --output-format '')\"\n"
      // Provider unavailable: the binary is off PATH, so the dynamic slots go
      // quiet while the embedded candidate data keeps answering.
      << "export PATH=/usr/bin:/bin\n"
      << "echo \"TNAMEOFF: $(probe encro -T '')\"\n"
      << "echo \"TPLSTATICOFF: $(probe encro --output-format '')\"\n"
      << "echo \"KEYSOFF: $(probe encro template unset anime '')\"\n"
      // Empty store: nothing to offer, and still no file fallback.
      << "export PATH=\"$(cygpath -u '"
      << forwardSlashes(pathutf8::fromPath(executableDir()))
      << "'):$PATH\"\n"
      << "export ENCRO_CONFIG=\""
      << forwardSlashes(pathutf8::fromPath(emptyConfig))
      << "\"\n"
      << "echo \"TNAMEEMPTY: $(probe encro -T '')\"\n";
  }

  auto const [exitCode, output] =
    runCapture("bash \"" + forwardSlashes(pathutf8::fromPath(driver)) + "\"");
  REQUIRE(exitCode == 0);
  CHECK(output.find("ENUM: mp4 webp") != std::string::npos);
  // --restart is excluded by the typed --resume; --recursive still offered.
  CHECK(output.find("HIDDEN: --recursive --resume") != std::string::npos);
  CHECK(output.find("HIDDEN: --restart") == std::string::npos);
  CHECK(output.find("SUB: preview") != std::string::npos);
  // Config action verbs complete at the verb slot; keys and then key values
  // complete after the set verb.
  CHECK(output.find("CFGVERB: list get set unset path\n") != std::string::npos);
  CHECK(output.find("CFGKEY: color") != std::string::npos);
  CHECK(output.find(" folder-summary") != std::string::npos);
  CHECK(output.find("CFGVAL: mp4 webp") != std::string::npos);
  // after a completed set pair, only the remaining config options are offered
  CHECK(output.find("SETDONE: --help\n") != std::string::npos);
  // Subcommand positional slots: enum candidates, flag-order independence,
  // past-the-end suppression, and path-positional fall-through.
  CHECK(output.find("POSBARE: bash powershell\n") != std::string::npos);
  CHECK(output.find("POSPREFIX: powershell") != std::string::npos);
  CHECK(output.find("POSFLAG: bash powershell") != std::string::npos);
  CHECK(output.find("POSDONE: SUPPRESSED") != std::string::npos);
  CHECK(output.find("POSNOMATCH: SUPPRESSED") != std::string::npos);
  // X is --model-dir's value, so the dir positional slot stays unfilled: the
  // function falls through (no suppression), leaving file completion to bash.
  CHECK(output.find("POSPATH: SUPPRESSED") == std::string::npos);
  CHECK(output.find("POSPATH: --") == std::string::npos);
  // Dynamic template-name slots read the live store through the binary, so the
  // candidates are the stored names, filtered by the typed prefix, and never
  // the marker .json file in the working directory.
  CHECK(lineStartingWith(output, "TNAME: ") == "TNAME: anime webp");
  CHECK(lineStartingWith(output, "TNAMEPREFIX: ") == "TNAMEPREFIX: anime");
  CHECK(lineStartingWith(output, "TEMPLONGFORM: ") == "TEMPLONGFORM: webp");
  CHECK(lineStartingWith(output, "SHOWNAME: ") == "SHOWNAME: anime webp");
  CHECK(lineStartingWith(output, "DELPREFIX: ") == "DELPREFIX: anime");
  auto const keys = lineStartingWith(output, "KEYS: ");
  CHECK(keys.find(" crf") != std::string::npos);
  CHECK(keys.find(" preset") != std::string::npos);
  CHECK(keys.find("input") == std::string::npos);
  CHECK(lineStartingWith(output, "KEYPREFIX: ") == "KEYPREFIX: crf");
  // Candidates belong to the slot, not the action: the name slot is filled
  // whatever verb was typed, and a second -T still completes the value slot.
  CHECK(lineStartingWith(output, "UNSETSLOT: ") == "UNSETSLOT: anime webp");
  CHECK(lineStartingWith(output, "LISTSLOT: ") == "LISTSLOT: anime webp");
  CHECK(lineStartingWith(output, "TSECOND: ") == "TSECOND: anime webp");
  CHECK(
    lineStartingWith(output, "TPLVERB: ")
    == "TPLVERB: list show save unset delete path schema"
  );
  CHECK(lineStartingWith(output, "TPLSTATIC: ") == "TPLSTATIC: mp4 webp");
  // Binary off PATH: dynamic slots go quiet, embedded candidates still answer.
  CHECK(lineStartingWith(output, "TNAMEOFF: ") == "TNAMEOFF: SUPPRESSED");
  CHECK(lineStartingWith(output, "TPLSTATICOFF: ") == "TPLSTATICOFF: mp4 webp");
  CHECK(lineStartingWith(output, "KEYSOFF: ").find(" crf") != std::string::npos);
  // Empty store: nothing to offer, and no file fallback either.
  CHECK(lineStartingWith(output, "TNAMEEMPTY: ") == "TNAMEEMPTY: SUPPRESSED");
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

  // organize is only offered with a backend; the path-delegation probe below
  // needs its scope in the emitted script.
  auto const fake = testutils::ScopedEnvVar{"ENCRO_FAKE_TAGGER", "fixture.json"};

  TempDir temp;
  auto const scriptPath = emitScriptTo(temp, "powershell", "encro-pwsh.ps1");
  REQUIRE(testutils::readTextFile(scriptPath).find("organize") != std::string::npos);
  auto const scriptRef = "\"" + forwardSlashes(pathutf8::fromPath(scriptPath)) + "\"";

  // Known file so the empty-return file fallback lists something assertable.
  { auto stream = std::ofstream{temp.path / "zzz.txt", std::ios::binary}; }

  // The dynamic slots read the live template store through the binary on PATH,
  // so the store is seeded first; the marker `.json` file makes a broken
  // file-fallback suppression visible.
  auto const storeDir = temp.path / "store";
  auto const cwd = temp.path / "cwd";
  auto const emptyConfig = temp.path / "empty" / "config.json";
  std::error_code ec;
  fs::create_directories(cwd, ec);
  REQUIRE(fs::exists(cwd));
  { auto marker = std::ofstream{cwd / "marker.json", std::ios::binary}; }
  auto const configOverride =
    testutils::ScopedEnvVar{"ENCRO_CONFIG", pathutf8::fromPath(storeDir / "config.json")};
  REQUIRE(seedTemplateStore(storeDir, "template save anime --crf 24"));
  REQUIRE(seedTemplateStore(storeDir, "template save webp --preset p5"));

  auto const driver = temp.path / "drive-ps.ps1";
  {
    auto stream = std::ofstream{driver, std::ios::binary | std::ios::trunc};
    REQUIRE(stream.is_open());
    stream
      << "Set-Location -LiteralPath '"
      << forwardSlashes(pathutf8::fromPath(temp.path))
      << "'\n"
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
      << "echo \"CFGVERB: $(Probe 'encro config ')\"\n"
      << "echo \"CFGKEY: $(Probe 'encro config set ')\"\n"
      << "echo \"CFGVAL: $(Probe 'encro config set output-format ')\"\n"
      << "echo \"SCOPE: $(Probe 'encro preview --')\"\n"
      << "echo \"SETDONE: $(Probe 'encro config set jobs 4 --')\"\n"
      << "echo \"POSBARE: $(Probe 'encro completion ')\"\n"
      << "echo \"POSPREFIX: $(Probe 'encro completion p')\"\n"
      << "echo \"POSFLAG: $(Probe 'encro completion --install ')\"\n"
      << "echo \"POSDONE: $(Probe 'encro completion bash zz')\"\n"
      << "echo \"POSPATH: $(Probe 'encro organize --model-dir X ')\"\n"
      // Dynamic template-name slots: the glue runs `encro completion
      // --candidates`, so the store probes need the build dir on PATH.
      << "$env:PATH = '"
      << forwardSlashes(pathutf8::fromPath(executableDir()))
      << ";' + $env:PATH\n"
      << "Set-Location -LiteralPath '"
      << forwardSlashes(pathutf8::fromPath(cwd))
      << "'\n"
      << "echo \"TNAME: $(Probe 'encro -T ')\"\n"
      << "echo \"TNAMEPREFIX: $(Probe 'encro -T a')\"\n"
      << "echo \"TEMPLONGFORM: $(Probe 'encro --template w')\"\n"
      << "echo \"SHOWNAME: $(Probe 'encro template show ')\"\n"
      << "echo \"DELPREFIX: $(Probe 'encro template delete a')\"\n"
      << "echo \"KEYS: $(Probe 'encro template unset anime ')\"\n"
      << "echo \"KEYPREFIX: $(Probe 'encro template unset anime cr')\"\n"
      << "echo \"UNSETSLOT: $(Probe 'encro template unset ')\"\n"
      << "echo \"LISTSLOT: $(Probe 'encro template list ')\"\n"
      << "echo \"TSECOND: $(Probe 'encro -T anime -T ')\"\n"
      << "echo \"TPLVERB: $(Probe 'encro template ')\"\n"
      << "echo \"TPLSTATIC: $(Probe 'encro --output-format ')\"\n"
      // Provider unavailable: the binary is off PATH, so the dynamic slots go
      // quiet while the embedded candidate data keeps answering.
      << "$env:PATH = $env:SystemRoot + '\\System32'\n"
      << "echo \"TNAMEOFF: $(Probe 'encro -T ')\"\n"
      << "echo \"TPLSTATICOFF: $(Probe 'encro --output-format ')\"\n"
      << "echo \"KEYSOFF: $(Probe 'encro template unset anime ')\"\n"
      // Empty store: nothing to offer, and still no file fallback.
      << "$env:PATH = '"
      << forwardSlashes(pathutf8::fromPath(executableDir()))
      << ";' + $env:PATH\n"
      << "$env:ENCRO_CONFIG = '"
      << forwardSlashes(pathutf8::fromPath(emptyConfig))
      << "'\n"
      << "echo \"TNAMEEMPTY: $(Probe 'encro -T ')\"\n";
  }

  auto const [exitCode, output] = runCapture(
    *shell
    + " -NoProfile -ExecutionPolicy Bypass -File \""
    + forwardSlashes(pathutf8::fromPath(driver))
    + "\""
  );
  REQUIRE(exitCode == 0);
  CHECK(output.find("ENUM: mp4,webp") != std::string::npos);
  CHECK(output.find("HIDDEN: --recursive,--resume") != std::string::npos);
  CHECK(output.find("HIDDEN: --restart") == std::string::npos);
  CHECK(output.find("SUB: preview") != std::string::npos);
  CHECK(output.find("CFGVERB: list,get,set,unset,path") != std::string::npos);
  CHECK(output.find("CFGKEY: color,compress,crf") != std::string::npos);
  CHECK(output.find("CFGVAL: mp4,webp") != std::string::npos);
  // preview scope: its own options only, no main-command --pack
  CHECK(output.find("--pack") == std::string::npos);
  CHECK(output.find("--start") != std::string::npos);
  CHECK(output.find("SETDONE: --help") != std::string::npos);
  // Subcommand positional slots: enum candidates, flag-order independence,
  // past-the-end self-echo (no candidates), and file delegation.
  CHECK(output.find("POSBARE: bash,powershell") != std::string::npos);
  CHECK(output.find("POSPREFIX: powershell") != std::string::npos);
  CHECK(output.find("POSFLAG: bash,powershell") != std::string::npos);
  CHECK(output.find("POSDONE: zz") != std::string::npos);
  // X is --model-dir's value, so the dir slot stays unfilled and the empty
  // return falls back to native file completion (5.1 prefixes ".\" , PS7
  // does not, so match within the line).
  auto const pospathPos = output.find("POSPATH: ");
  REQUIRE(pospathPos != std::string::npos);
  auto const pospathLine =
    output.substr(pospathPos, output.find('\n', pospathPos) - pospathPos);
  CHECK(pospathLine.find("zzz.txt") != std::string::npos);
  CHECK(pospathLine.find("--") == std::string::npos);
  // Dynamic template-name slots read the live store through the binary; the
  // candidates are the stored names, filtered by the typed prefix, and never
  // the marker .json file in the working directory.
  CHECK(lineStartingWith(output, "TNAME: ") == "TNAME: anime,webp");
  CHECK(lineStartingWith(output, "TNAMEPREFIX: ") == "TNAMEPREFIX: anime");
  CHECK(lineStartingWith(output, "TEMPLONGFORM: ") == "TEMPLONGFORM: webp");
  CHECK(lineStartingWith(output, "SHOWNAME: ") == "SHOWNAME: anime,webp");
  CHECK(lineStartingWith(output, "DELPREFIX: ") == "DELPREFIX: anime");
  auto const keys = lineStartingWith(output, "KEYS: ");
  CHECK(keys.find(",crf") != std::string::npos);
  CHECK(keys.find("preset") != std::string::npos);
  CHECK(keys.find("input") == std::string::npos);
  CHECK(lineStartingWith(output, "KEYPREFIX: ") == "KEYPREFIX: crf");
  // Candidates belong to the slot, not the action: the name slot is filled
  // whatever verb was typed, and a second -T still completes the value slot.
  CHECK(lineStartingWith(output, "UNSETSLOT: ") == "UNSETSLOT: anime,webp");
  CHECK(lineStartingWith(output, "LISTSLOT: ") == "LISTSLOT: anime,webp");
  CHECK(lineStartingWith(output, "TSECOND: ") == "TSECOND: anime,webp");
  CHECK(
    lineStartingWith(output, "TPLVERB: ")
    == "TPLVERB: list,show,save,unset,delete,path,schema"
  );
  CHECK(lineStartingWith(output, "TPLSTATIC: ") == "TPLSTATIC: mp4,webp");
  // Binary off PATH: dynamic slots go quiet, embedded candidates still answer.
  CHECK(lineStartingWith(output, "TNAMEOFF: ") == "TNAMEOFF:  ");
  CHECK(lineStartingWith(output, "TPLSTATICOFF: ") == "TPLSTATICOFF: mp4,webp");
  CHECK(lineStartingWith(output, "KEYSOFF: ").find(",crf") != std::string::npos);
  // Empty store: nothing to offer, and no file fallback either.
  CHECK(lineStartingWith(output, "TNAMEEMPTY: ") == "TNAMEEMPTY:  ");
}
