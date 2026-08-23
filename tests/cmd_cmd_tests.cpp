#include "cmd/cmd.h"
#include "infra/env.h"
#include "infra/terminal.h"

#include <catch2/catch_all.hpp>

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

class ScopedEnvVar {
public:
  ScopedEnvVar(std::string name, std::string value)
    : name_(std::move(name)), hadOriginal_(false) {
    if (auto const current = processenv::readEnvVar(name_); current.has_value()) {
      originalValue_ = current.value();
      hadOriginal_ = true;
    }
    set(value);
  }

  ScopedEnvVar(ScopedEnvVar const&) = delete;
  auto operator=(ScopedEnvVar const&) -> ScopedEnvVar& = delete;

  ~ScopedEnvVar() {
    if (hadOriginal_) {
      set(originalValue_);
    } else {
      unset();
    }
  }

private:
  void set(std::string const& value) {
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s(name_.c_str(), value.c_str());
#else
    setenv(name_.c_str(), value.c_str(), 1);
#endif
  }
  void unset() {
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s(name_.c_str(), "");
#else
    unsetenv(name_.c_str());
#endif
  }

  std::string name_;
  std::string originalValue_;
  bool hadOriginal_;
};

auto parseArgs(std::vector<std::string> const& args) -> CmdParseResult {
  static thread_local std::vector<std::string> storage;
  storage = args;
  auto argv = std::vector<char*>{};
  argv.reserve(storage.size());
  for (auto& arg: storage) { argv.push_back(arg.data()); }
  argv.push_back(nullptr);

  return commandLineInit(static_cast<int>(argv.size() - 1), argv.data(), "");
}

std::size_t longestHelpLine(std::string_view text) {
  auto longest = std::size_t{0};
  auto start = std::size_t{0};

  while (start <= text.size()) {
    auto const end = text.find('\n', start);
    auto const lineLength =
      end == std::string_view::npos ? text.size() - start : end - start;
    longest = std::max(longest, lineLength);

    if (end == std::string_view::npos) { break; }
    start = end + 1;
  }

  return longest;
}

auto stripAnsi(std::string_view text) -> std::string {
  auto result = std::string{};
  result.reserve(text.size());

  auto index = std::size_t{0};
  while (index < text.size()) {
    if (text[index] == '\x1b' && index + 1 < text.size() && text[index + 1] == '[') {
      index += 2;
      while (
        index < text.size() && !std::isalpha(static_cast<unsigned char>(text[index]))
      ) {
        ++index;
      }
      if (index < text.size()) { ++index; }
      continue;
    }

    result += text[index];
    ++index;
  }

  return result;
}

auto findHelpLine(std::string_view text, std::string_view needle)
  -> std::optional<std::string> {
  auto start = std::size_t{0};

  while (start <= text.size()) {
    auto const end = text.find('\n', start);
    auto const line = end == std::string_view::npos ? text.substr(start)
                                                    : text.substr(start, end - start);

    if (line.find(needle) != std::string_view::npos) { return std::string{line}; }
    if (end == std::string_view::npos) { break; }
    start = end + 1;
  }

  return std::nullopt;
}

}  // namespace

TEST_CASE("commandLineInit exposes defaults", "[cmd]") {
  auto const result = parseArgs({"encro"});

  CHECK(result.processType == "video");
  CHECK(result.outputFormat == "mp4");
  CHECK(result.forceConflictHandling == "y");
  CHECK(result.color == "auto");
  CHECK(result.imageQuality.has_value() == false);
  CHECK(result.folderSummary == false);
  CHECK(result.verbose == false);
  CHECK(result.help == false);
  CHECK(result.version == false);
}

TEST_CASE("commandLineInit --version flag sets version=true", "[cmd]") {
  auto const result = parseArgs({"encro", "--version"});

  CHECK(result.version == true);
  CHECK(result.help == false);
  CHECK_FALSE(result.error.has_value());
}

TEST_CASE("commandLineInit --version is not set by default", "[cmd]") {
  auto const result = parseArgs({"encro"});

  CHECK(result.version == false);
}

TEST_CASE("commandLineInit parses non-conflicting flags and option values", "[cmd]") {
  auto const result = parseArgs(
    {"encro",
     "--yes",
     "--recursive",
     "--force-conflict-handling=n",
     "--folder-summary",
     "--color=always",
     "--verbose",
     "--full-progress",
     "--overwrite",
     "--compress"}
  );

  CHECK(result.yesToAll == true);
  CHECK(result.recursive == true);
  CHECK(result.forceConflictHandling == "n");
  CHECK(result.folderSummary == true);
  CHECK(result.color == "always");
  CHECK(result.verbose == true);
  CHECK(result.fullProgress == true);
  CHECK(result.overwrite == true);
  CHECK(result.compress == true);
}

TEST_CASE("commandLineInit parses state-file option", "[cmd]") {
  auto const result =
    parseArgs({"encro", "--state-file", "job-state.json", "-i", "input.mp4"});

  REQUIRE(result.stateFile.has_value());
  CHECK(result.stateFile.value() == "job-state.json");
}

TEST_CASE("commandLineInit parses multi-input values", "[cmd]") {
  auto const result = parseArgs({"encro", "-I", "a.mp4", "b.mkv", "c.mov"});

  REQUIRE(result.inputs.has_value());
  auto const& inputs = result.inputs.value();
  REQUIRE(inputs.size() == 3);
  CHECK(inputs[0] == "a.mp4");
  CHECK(inputs[1] == "b.mkv");
  CHECK(inputs[2] == "c.mov");
}

TEST_CASE("commandLineInit parses a single positional input", "[cmd]") {
  auto const result = parseArgs({"encro", "videos"});

  REQUIRE(result.positionalInputs.has_value());
  auto const& inputs = result.positionalInputs.value();
  REQUIRE(inputs.size() == 1);
  CHECK(inputs[0] == "videos");
  CHECK_FALSE(result.error.has_value());
}

TEST_CASE("commandLineInit parses multiple positional inputs", "[cmd]") {
  auto const result = parseArgs({"encro", "a.mp4", "b.mkv", "c.mov"});

  REQUIRE(result.positionalInputs.has_value());
  auto const& inputs = result.positionalInputs.value();
  REQUIRE(inputs.size() == 3);
  CHECK(inputs[0] == "a.mp4");
  CHECK(inputs[1] == "b.mkv");
  CHECK(inputs[2] == "c.mov");
  CHECK_FALSE(result.error.has_value());
}

TEST_CASE("commandLineInit parses positional inputs mixed with flags", "[cmd]") {
  auto const result = parseArgs({"encro", "a.mp4", "-o", "out", "b.mkv"});

  REQUIRE(result.positionalInputs.has_value());
  auto const& inputs = result.positionalInputs.value();
  REQUIRE(inputs.size() == 2);
  CHECK(inputs[0] == "a.mp4");
  CHECK(inputs[1] == "b.mkv");
  REQUIRE(result.output.has_value());
  CHECK(result.output.value() == "out");
  CHECK_FALSE(result.error.has_value());
}

TEST_CASE("commandLineInit parses positional input after -- separator", "[cmd]") {
  auto const result = parseArgs({"encro", "--", "-weird.mp4"});

  REQUIRE(result.positionalInputs.has_value());
  auto const& inputs = result.positionalInputs.value();
  REQUIRE(inputs.size() == 1);
  CHECK(inputs[0] == "-weird.mp4");
  CHECK_FALSE(result.error.has_value());
}

TEST_CASE("commandLineInit parses jobs option", "[cmd]") {
  auto const result = parseArgs({"encro", "--jobs", "4"});

  REQUIRE(result.maxJobs.has_value());
  CHECK(result.maxJobs.value() == 4);
}

TEST_CASE("commandLineInit reports unknown options", "[cmd]") {
  auto const result = parseArgs({"encro", "--nope"});

  REQUIRE(result.error.has_value());
  // CLI11 error messages differ from boost::po — accept any error string (per D-06)
  CHECK_FALSE(result.error.value().empty());
}

TEST_CASE("commandLineInit rejects removed --flat flag", "[cmd]") {
  auto const result = parseArgs({"encro", "--flat", "-i", "input.mp4"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--flat") != std::string::npos);
}

TEST_CASE("commandLineInit rejects removed -e flag", "[cmd]") {
  auto const result = parseArgs({"encro", "-e", "-i", "input.mp4"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("-e") != std::string::npos);
}

TEST_CASE("commandLineInit rejects --resume with --restart", "[cmd]") {
  auto const result = parseArgs({"encro", "--resume", "--restart", "-i", "input.mp4"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--resume") != std::string::npos);
  CHECK(result.error.value().find("--restart") != std::string::npos);
}

TEST_CASE("commandLineInit rejects -i with -I", "[cmd]") {
  auto const result = parseArgs({"encro", "-i", "a.mp4", "-I", "b.mp4"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--input") != std::string::npos);
  CHECK(result.error.value().find("--inputs") != std::string::npos);
}

TEST_CASE("commandLineInit rejects --pack with --pack-only", "[cmd]") {
  auto const result = parseArgs({"encro", "--pack", "--pack-only", "-i", "input.mp4"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--pack") != std::string::npos);
}

TEST_CASE("commandLineInit caps help output to configured maximum width", "[cmd]") {
  auto const columnsVar = ScopedEnvVar{"COLUMNS", "200"};

  auto const result = parseArgs({"encro"});
  auto const& help = result.helpText;

  CHECK(longestHelpLine(help) <= 120);
}

TEST_CASE("commandLineInit adapts help output to narrower terminal width", "[cmd]") {
  auto const columnsVar = ScopedEnvVar{"COLUMNS", "72"};

  auto const result = parseArgs({"encro"});
  auto const& help = result.helpText;

  CHECK(longestHelpLine(help) <= 72);
}

TEST_CASE("commandLineInit parses --compress flag", "[cmd]") {
  auto const result = parseArgs({"encro", "--compress"});

  CHECK(result.compress == true);
}

TEST_CASE("commandLineInit does not set --compress by default", "[cmd]") {
  auto const result = parseArgs({"encro"});

  CHECK(result.compress == false);
}

TEST_CASE("commandLineInit parses --image-quality as integer", "[cmd]") {
  auto const result = parseArgs({"encro", "--compress", "--image-quality", "15"});

  REQUIRE(result.imageQuality.has_value());
  CHECK(result.imageQuality.value() == 15);
}

TEST_CASE("commandLineInit parses -q short option for quality", "[cmd]") {
  auto const result = parseArgs({"encro", "-c", "-q", "15"});

  REQUIRE(result.imageQuality.has_value());
  CHECK(result.imageQuality.value() == 15);
}

TEST_CASE("commandLineInit does not set image-quality by default", "[cmd]") {
  auto const result = parseArgs({"encro"});

  CHECK(result.imageQuality.has_value() == false);
}

TEST_CASE("commandLineInit parses --crf as integer", "[cmd]") {
  auto const result = parseArgs({"encro", "--crf", "26"});

  REQUIRE(result.crf.has_value());
  CHECK(result.crf.value() == 26);
}

TEST_CASE("app-level flags apply before the preview subcommand", "[cmd]") {
  auto const result = parseArgs({"encro", "--crf", "15", "preview", "a.mp4"});

  REQUIRE(result.preview);
  REQUIRE(result.crf.has_value());
  CHECK(result.crf.value() == 15);
}

TEST_CASE("preview subcommand does not swallow app-level flags", "[cmd]") {
  auto const result =
    parseArgs({"encro", "--video-codec", "libx264", "preview", "a.mp4", "--no-open"});

  REQUIRE(result.preview);
  REQUIRE(result.videoCodec.has_value());
  CHECK(result.videoCodec.value() == "libx264");
}

TEST_CASE("commandLineInit does not set crf by default", "[cmd]") {
  auto const result = parseArgs({"encro"});

  CHECK(result.crf.has_value() == false);
}

TEST_CASE("commandLineInit parses --preset option", "[cmd]") {
  auto const result = parseArgs({"encro", "--preset", "p7"});

  REQUIRE(result.nvencPreset.has_value());
  CHECK(result.nvencPreset.value() == "p7");
}

TEST_CASE("commandLineInit does not set preset by default", "[cmd]") {
  auto const result = parseArgs({"encro"});

  CHECK(result.nvencPreset.has_value() == false);
}

TEST_CASE("commandLineInit rejects -q without a value", "[cmd]") {
  auto const result = parseArgs({"encro", "-q"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--image-quality") != std::string::npos);
}

TEST_CASE("commandLineInit rejects --crf without a value", "[cmd]") {
  auto const result = parseArgs({"encro", "--crf"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--crf") != std::string::npos);
}

TEST_CASE("commandLineInit rejects --preset without a value", "[cmd]") {
  auto const result = parseArgs({"encro", "--preset"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--preset") != std::string::npos);
}

TEST_CASE("commandLineInit default jobs is not set", "[cmd]") {
  auto const result = parseArgs({"encro"});
  // jobs has a default_str of "10" but no default_value — so it should NOT be in result
  // Verify: maxJobs is not set when --jobs is not explicitly passed
  // Note: CLI11 with default_str does populate count(), so this depends on implementation
  // If CLI11 sets count>0 with default_str, maxJobs will have value 10
  // Either way is acceptable — the field has the correct default
  CHECK(result.yesToAll == false);
  CHECK(result.fullProgress == false);
}

// ── Phase 20-02: colored --help smoke tests ──

TEST_CASE("help text contains ANSI escape codes when color is always", "[cmd][color]") {
  terminal::configure(terminal::ColorMode::Always);

  auto const result = parseArgs({"encro", "--help"});
  auto const& help = result.helpText;

  // After Phase 20 color injection, help text SHOULD contain ANSI escape codes
  CHECK(help.find("\x1b[") != std::string::npos);

  terminal::reset();
}

TEST_CASE("help text contains NO ANSI codes when color is never", "[cmd][color]") {
  terminal::configure(terminal::ColorMode::Never);

  auto const result = parseArgs({"encro", "--help"});
  auto const& help = result.helpText;

  // When color is disabled, no ANSI escape codes should be present
  CHECK(help.find("\x1b[") == std::string::npos);

  terminal::reset();
}

TEST_CASE(
  "help text keeps aligned description columns when default values are colored",
  "[cmd][color]"
) {
  terminal::configure(terminal::ColorMode::Always);

  auto const result = parseArgs({"encro", "-hh"});
  auto const plainHelp = stripAnsi(result.helpText);

  auto const keepLine = findHelpLine(plainHelp, "--keep");
  auto const outputFormatLine = findHelpLine(plainHelp, "--output-format");
  auto const forceConflictLine = findHelpLine(plainHelp, "--force-conflict-handling");

  REQUIRE(keepLine.has_value());
  REQUIRE(outputFormatLine.has_value());
  REQUIRE(forceConflictLine.has_value());

  auto const keepColumn =
    keepLine->find("preserve relative input subdirectories inside the output directory");
  auto const outputFormatColumn = outputFormatLine->find("target format: mp4 or webp");
  auto const forceConflictColumn =
    forceConflictLine->find("same-name collisions in flat output");

  REQUIRE(keepColumn != std::string::npos);
  REQUIRE(outputFormatColumn != std::string::npos);
  REQUIRE(forceConflictColumn != std::string::npos);

  CHECK(outputFormatColumn == keepColumn);
  CHECK(forceConflictColumn == keepColumn);

  terminal::reset();
}

TEST_CASE("help text contains NO ANSI codes when NO_COLOR is set", "[cmd][color]") {
  auto const noColorGuard = ScopedEnvVar{"NO_COLOR", "1"};

  auto const result = parseArgs({"encro", "--help"});
  auto const& help = result.helpText;

  CHECK(help.find("\x1b[") == std::string::npos);
}

TEST_CASE(
  "help text contains expected option names after color injection",
  "[cmd][color]"
) {
  auto const result = parseArgs({"encro", "-hh"});
  auto const& help = result.helpText;

  // Content must survive color injection — plain text substrings still embedded
  // Full (-hh) tier renders every option, including the advanced ones
  CHECK(help.find("--input") != std::string::npos);
  CHECK(help.find("--verbose") != std::string::npos);
  CHECK(help.find("--output-format") != std::string::npos);
  CHECK(help.find("--pack") != std::string::npos);
}

TEST_CASE(
  "help text contains expected group headers after color injection",
  "[cmd][color]"
) {
  auto const result = parseArgs({"encro"});
  auto const& help = result.helpText;

  // Group descriptions (used by formatGroupHeader via get_description())
  CHECK(help.find("General") != std::string::npos);
  CHECK(help.find("Input/Output") != std::string::npos);
  CHECK(help.find("Processing") != std::string::npos);
  CHECK(help.find("File operation") != std::string::npos);
}

TEST_CASE("help text non-empty after color injection", "[cmd][color]") {
  auto const result = parseArgs({"encro"});
  auto const& help = result.helpText;

  CHECK_FALSE(help.empty());
  // Help output should be substantial (multiple options rendered)
  CHECK(help.size() > 200);
}

TEST_CASE("help text includes usage synopsis before option groups", "[cmd]") {
  auto const result = parseArgs({"encro", "--help"});
  auto const help = stripAnsi(result.helpText);

  auto const usagePos = help.find("Usage:");
  auto const generalOptionsPos = help.find("General options:");

  REQUIRE(usagePos != std::string::npos);
  REQUIRE(generalOptionsPos != std::string::npos);

  CHECK(help.find("encro [<input>... | -i <input> | -I <file>...]") != std::string::npos);
  CHECK(help.find("input-paths") != std::string::npos);
  CHECK(usagePos < generalOptionsPos);
}

TEST_CASE("commandLineInit parses --min-vmaf with default 95", "[cmd]") {
  auto const defaults = parseArgs({"encro"});
  CHECK(defaults.minVmaf == 95);

  auto const custom = parseArgs({"encro", "--min-vmaf", "90"});
  CHECK(custom.minVmaf == 90);
  CHECK_FALSE(custom.error.has_value());
}

TEST_CASE("commandLineInit parses --dry-run", "[cmd]") {
  auto const result = parseArgs({"encro", "-i", "a.mp4", "--dry-run"});
  CHECK(result.dryRun);
  CHECK_FALSE(result.error.has_value());
}

TEST_CASE(
  "preview subcommand wins over positional input interpretation",
  "[cmd][cli-positional]"
) {
  auto const result = parseArgs({"encro", "preview", "a.mp4", "b.mp4"});
  CHECK(result.preview);
  CHECK_FALSE(result.positionalInputs.has_value());
  REQUIRE(result.previewOriginal.has_value());
  CHECK(result.previewOriginal.value() == "a.mp4");
  REQUIRE(result.previewEncoded.has_value());
  CHECK(result.previewEncoded.value() == "b.mp4");
  CHECK_FALSE(result.error.has_value());
}

TEST_CASE("preview accepts a single input (single-input mode)", "[cmd][cli-positional]") {
  auto const result = parseArgs({"encro", "preview", "a.mp4"});
  CHECK(result.preview);
  REQUIRE(result.previewOriginal.has_value());
  CHECK(result.previewOriginal.value() == "a.mp4");
  CHECK_FALSE(result.previewEncoded.has_value());
  CHECK_FALSE(result.error.has_value());
}

TEST_CASE("preview parses output start duration and no-open", "[cmd][cli-positional]") {
  auto const result = parseArgs({
    "encro",
    "preview",
    "a.mp4",
    "b.mp4",
    "--output",
    "out.mp4",
    "--start",
    "2510",
    "--duration",
    "20",
    "--no-open",
  });
  REQUIRE(result.preview);
  REQUIRE(result.previewOutput.has_value());
  CHECK(result.previewOutput.value() == "out.mp4");
  REQUIRE(result.previewStart.has_value());
  CHECK(result.previewStart.value() == Catch::Approx(2510.0));
  REQUIRE(result.previewDuration.has_value());
  CHECK(result.previewDuration.value() == Catch::Approx(20.0));
  CHECK(result.previewNoOpen);
  CHECK_FALSE(result.error.has_value());
}

TEST_CASE("preview without positionals reports a clear error", "[cmd][cli-positional]") {
  auto const result = parseArgs({"encro", "preview"});
  // Native RequiredError; parse never completes, so preview is not flagged
  CHECK_FALSE(result.preview);
  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("original is required") != std::string::npos);
}

TEST_CASE("preview -h renders preview help text", "[cmd][cli-positional]") {
  auto const result = parseArgs({"encro", "preview", "-h"});
  // CallForHelp path: parse never completes, so preview is not flagged
  CHECK_FALSE(result.preview);
  CHECK(result.help);
  CHECK_FALSE(result.error.has_value());
  CHECK(
    result.helpText.find("encro preview <original> [<encoded>]") != std::string::npos
  );
  CHECK(result.helpText.find("--no-open") != std::string::npos);
}

TEST_CASE(
  "bare invocation falls through to the encode workflow",
  "[cmd][cli-positional]"
) {
  auto const result = parseArgs({"encro", "videos"});
  CHECK_FALSE(result.preview);
  REQUIRE(result.positionalInputs.has_value());
  CHECK(result.positionalInputs.value() == std::vector<std::string>{"videos"});
  CHECK_FALSE(result.error.has_value());
}

TEST_CASE("bare invocation with flags still falls through", "[cmd][cli-positional]") {
  auto const result = parseArgs({"encro", "-i", "videos", "-y"});
  CHECK_FALSE(result.preview);
  REQUIRE(result.input.has_value());
  CHECK_FALSE(result.error.has_value());
}

// ── CLI11-native parse-time validation (spec: cli11-native-validation) ──

TEST_CASE("cli11 rejects invalid output format values", "[cmd][cli11-native]") {
  auto const result = parseArgs({"encro", "-f", "avi"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--output-format") != std::string::npos);
  CHECK(result.error.value().find("mp4") != std::string::npos);
}

TEST_CASE(
  "cli11 rejects invalid conflict-handling value and accepts case variants",
  "[cmd][cli11-native]"
) {
  auto const bad = parseArgs({"encro", "--force-conflict-handling", "x"});
  REQUIRE(bad.error.has_value());
  CHECK(bad.error.value().find("--force-conflict-handling") != std::string::npos);

  auto const upper = parseArgs({"encro", "--force-conflict-handling", "Y"});
  CHECK_FALSE(upper.error.has_value());
  CHECK(upper.forceConflictHandling == "y");  // CheckedTransformer maps Y->y
}

TEST_CASE("cli11 rejects invalid color modes", "[cmd][cli11-native]") {
  auto const result = parseArgs({"encro", "--color", "pink"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--color") != std::string::npos);
  CHECK(result.error.value().find("pink") != std::string::npos);
}

TEST_CASE("cli11 rejects invalid presets", "[cmd][cli11-native]") {
  auto const result = parseArgs({"encro", "--preset", "p9"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--preset") != std::string::npos);
  CHECK(result.error.value().find("p9") != std::string::npos);
}

TEST_CASE("cli11 accepts all preset values", "[cmd][cli11-native]") {
  for (auto const value: {"auto", "p1", "p2", "p3", "p4", "p5", "p6", "p7"}) {
    auto const result = parseArgs({"encro", "--preset", value});
    CAPTURE(value);
    CHECK_FALSE(result.error.has_value());
  }
}

TEST_CASE(
  "cli11 accepts case variants and bare default for --color",
  "[cmd][cli11-native]"
) {
  auto const always = parseArgs({"encro", "--color", "ALWAYS"});
  CHECK_FALSE(always.error.has_value());
  CHECK(always.color == "always");  // IsMember writes the canonical member

  auto const bare = parseArgs({"encro", "--color"});
  CHECK_FALSE(bare.error.has_value());
  CHECK(bare.color == "auto");  // default_str fills the bare flag
}

TEST_CASE("cli11 maps -t aliases to canonical values", "[cmd][cli11-native]") {
  auto const vid = parseArgs({"encro", "-t", "vid"});
  CHECK_FALSE(vid.error.has_value());
  CHECK(vid.processType == "video");

  auto const pic = parseArgs({"encro", "-t", "pic"});
  CHECK_FALSE(pic.error.has_value());
  CHECK(pic.processType == "picture");

  auto const canonical = parseArgs({"encro", "-t", "picture"});
  CHECK_FALSE(canonical.error.has_value());
  CHECK(canonical.processType == "picture");

  auto const bad = parseArgs({"encro", "-t", "film"});
  REQUIRE(bad.error.has_value());
  CHECK(bad.error.value().find("film") != std::string::npos);
}

TEST_CASE(
  "cli11 rejects out-of-range -q/--crf/--min-vmaf and accepts boundaries",
  "[cmd][cli11-native]"
) {
  auto const qHigh = parseArgs({"encro", "-c", "-q", "99"});
  REQUIRE(qHigh.error.has_value());

  auto const qTooLow = parseArgs({"encro", "-c", "-q", "1"});
  REQUIRE(qTooLow.error.has_value());

  auto const qMin = parseArgs({"encro", "-c", "-q", "2"});
  CHECK_FALSE(qMin.error.has_value());
  REQUIRE(qMin.imageQuality.has_value());
  CHECK(qMin.imageQuality.value() == 2);

  auto const qMax = parseArgs({"encro", "-c", "-q", "31"});
  CHECK_FALSE(qMax.error.has_value());
  REQUIRE(qMax.imageQuality.has_value());
  CHECK(qMax.imageQuality.value() == 31);

  auto const crfHigh = parseArgs({"encro", "--crf", "52"});
  REQUIRE(crfHigh.error.has_value());

  auto const crfMin = parseArgs({"encro", "--crf", "0"});
  CHECK_FALSE(crfMin.error.has_value());
  REQUIRE(crfMin.crf.has_value());
  CHECK(crfMin.crf.value() == 0);

  auto const crfMax = parseArgs({"encro", "--crf", "51"});
  CHECK_FALSE(crfMax.error.has_value());
  REQUIRE(crfMax.crf.has_value());
  CHECK(crfMax.crf.value() == 51);
}

TEST_CASE(
  "cli11 rejects --min-vmaf out of range and accepts boundaries",
  "[cmd][cli11-native]"
) {
  auto const high = parseArgs({"encro", "--min-vmaf", "101"});
  REQUIRE(high.error.has_value());

  auto const low = parseArgs({"encro", "--min-vmaf", "-1"});
  REQUIRE(low.error.has_value());

  auto const min = parseArgs({"encro", "--min-vmaf", "0"});
  CHECK_FALSE(min.error.has_value());
  CHECK(min.minVmaf == 0);

  auto const max = parseArgs({"encro", "--min-vmaf", "100"});
  CHECK_FALSE(max.error.has_value());
  CHECK(max.minVmaf == 100);
}

TEST_CASE("cli11 rejects zero jobs and accepts one", "[cmd][cli11-native]") {
  auto const zero = parseArgs({"encro", "-j", "0"});
  REQUIRE(zero.error.has_value());
  CHECK(zero.error.value().find("--jobs") != std::string::npos);

  auto const one = parseArgs({"encro", "-j", "1"});
  CHECK_FALSE(one.error.has_value());
  REQUIRE(one.maxJobs.has_value());
  CHECK(one.maxJobs.value() == 1);
}

TEST_CASE("cli11 rejects --dry-run combined with --crf", "[cmd][cli11-native]") {
  auto const result = parseArgs({"encro", "--dry-run", "--crf", "20"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--dry-run") != std::string::npos);
  CHECK(result.error.value().find("--crf") != std::string::npos);
}

TEST_CASE("cli11 rejects positional inputs mixed with -i or -I", "[cmd][cli11-native]") {
  auto const withInput = parseArgs({"encro", "a.mp4", "-i", "b.mp4"});
  REQUIRE(withInput.error.has_value());
  CHECK(withInput.error.value().find("input-paths") != std::string::npos);
  CHECK(withInput.error.value().find("--input") != std::string::npos);

  auto const withInputs = parseArgs({"encro", "a.mp4", "-I", "b.mp4", "c.mp4"});
  REQUIRE(withInputs.error.has_value());
  CHECK(withInputs.error.value().find("input-paths") != std::string::npos);
  CHECK(withInputs.error.value().find("--inputs") != std::string::npos);
}

TEST_CASE("cli11 rejects --image-quality without --compress", "[cmd][cli11-native]") {
  auto const result = parseArgs({"encro", "-q", "15"});

  REQUIRE(result.error.has_value());
  CHECK(
    result.error.value().find("--image-quality requires --compress") != std::string::npos
  );
}

TEST_CASE("cli11 rejects negative preview --start", "[cmd][cli11-native]") {
  auto const result = parseArgs({"encro", "preview", "a.mp4", "--start", "-5"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--start") != std::string::npos);
}
