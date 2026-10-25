#include "core/path_utf8.h"
#include "cmd/cmd.h"
#include "cmd/completion_command.h"

#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Captured stdout split into lines, tolerating the CR a text-mode stdout adds
// on Windows (the generated scripts strip it for the same reason).
auto linesOf(std::string const& text) -> std::vector<std::string> {
  auto lines = std::vector<std::string>{};
  auto begin = std::size_t{0};
  while (begin < text.size()) {
    auto const newline = text.find('\n', begin);
    auto const stop = newline == std::string::npos ? text.size() : newline;
    auto line = text.substr(begin, stop - begin);
    if (!line.empty() && line.back() == '\r') { line.pop_back(); }
    if (!line.empty()) { lines.push_back(std::move(line)); }
    begin = stop + 1;
  }
  return lines;
}

struct ChannelRun {
  int exitCode = 0;
  std::vector<std::string> stdoutLines;
  std::string stderrText;
};

// Runs `encro completion --candidates <slot>` against the config path the env
// override in the calling test points at.
auto runChannel(std::string_view slot) -> ChannelRun {
  auto const temp = TempDir{};
  auto const outPath = temp.path / "stdout.txt";
  auto const errPath = temp.path / "stderr.txt";
  auto run = ChannelRun{};
  {
    auto const capture = testutils::StdoutCapture{outPath};
    {
      auto const errCapture = testutils::StderrCapture{errPath};
      run.exitCode = cmd::runCompletionCommand(
        testutils::parseArgs({"encro", "completion", "--candidates", std::string{slot}})
      );
    }
  }
  if (fs::exists(outPath)) {
    run.stdoutLines = linesOf(testutils::readTextFile(outPath));
  }
  if (fs::exists(errPath)) { run.stderrText = testutils::readTextFile(errPath); }
  return run;
}

// Stores one template file directly: the channel scans the directory only, so
// the content never matters (that is what keeps a malformed file harmless).
void writeTemplateFile(fs::path const& templatesDir, std::string_view fileName) {
  std::error_code ec;
  fs::create_directories(templatesDir, ec);
  auto stream =
    std::ofstream{templatesDir / fileName, std::ios::binary | std::ios::trunc};
  stream << "{\"$schema\": \"./templates.schema.json\"}\n";
}

}  // namespace

TEST_CASE("completion parse survives a corrupt config file", "[completion]") {
  TempDir temp;
  auto const configPath = temp.path / "broken.json";
  {
    auto stream = std::ofstream{configPath, std::ios::binary | std::ios::trunc};
    stream << "{not json";
  }
  // ScopedEnvVar restores the runner's pinned isolation path afterwards.
  testutils::ScopedEnvVar const
    envOverride{"ENCRO_CONFIG", pathutf8::fromPath(configPath)};

  auto const result = testutils::parseArgs({"encro", "completion", "bash"});

  REQUIRE_FALSE(result.error.has_value());
  CHECK(result.completion);
  CHECK(result.completionShell == "bash");
}

TEST_CASE("completion subcommand parses shell and actions", "[completion]") {
  auto const print = testutils::parseArgs({"encro", "completion", "bash"});
  REQUIRE_FALSE(print.error.has_value());
  CHECK(print.completion);
  CHECK(print.completionShell == "bash");
  CHECK_FALSE(print.completionInstall);
  CHECK_FALSE(print.completionUninstall);

  auto const pwsh = testutils::parseArgs({"encro", "completion", "powershell"});
  REQUIRE_FALSE(pwsh.error.has_value());
  CHECK(pwsh.completionShell == "powershell");
}

TEST_CASE("completion rejects unsupported shells", "[completion]") {
  auto const result = testutils::parseArgs({"encro", "completion", "zsh"});
  REQUIRE(result.error.has_value());
  // the parse error names the legal values (Members constraint)
  CHECK(result.error->find("bash") != std::string::npos);
  CHECK(result.error->find("powershell") != std::string::npos);
}

TEST_CASE("install and uninstall are mutually exclusive", "[completion]") {
  auto const result =
    testutils::parseArgs({"encro", "completion", "bash", "--install", "--uninstall"});
  REQUIRE(result.error.has_value());
}

TEST_CASE("completion help routing", "[completion]") {
  constexpr auto kSynopsis =
    std::string_view{"encro completion [--install | --uninstall] <powershell|bash>"};
  constexpr auto kExample = std::string_view{"encro completion powershell --install"};

  SECTION("bare completion shows the subcommand help") {
    auto const result = testutils::parseArgs({"encro", "completion"});
    REQUIRE_FALSE(result.error.has_value());
    CHECK(result.completion);
    CHECK(result.completionShell.empty());
    CHECK(result.helpText().find(kSynopsis) != std::string::npos);
    CHECK(result.helpText().find(kExample) != std::string::npos);
    CHECK(
      result.helpText().find("print, install, or uninstall shell completion scripts")
      != std::string::npos
    );
  }

  SECTION("-h routes through the help path") {
    auto const result = testutils::parseArgs({"encro", "completion", "-h"});
    CHECK(result.help);
    CHECK(result.helpText().find(kSynopsis) != std::string::npos);
    CHECK(result.helpText().find(kExample) != std::string::npos);
  }

  SECTION("--help routes through the help path") {
    auto const result = testutils::parseArgs({"encro", "completion", "--help"});
    CHECK(result.help);
    CHECK(result.helpText().find(kSynopsis) != std::string::npos);
  }
}

TEST_CASE(
  "both completion argument orders parse to the same install request",
  "[completion]"
) {
  auto const shellFirst =
    testutils::parseArgs({"encro", "completion", "powershell", "--install"});
  auto const flagFirst =
    testutils::parseArgs({"encro", "completion", "--install", "powershell"});

  REQUIRE_FALSE(shellFirst.error.has_value());
  REQUIRE_FALSE(flagFirst.error.has_value());
  CHECK(shellFirst.completionShell == "powershell");
  CHECK(flagFirst.completionShell == "powershell");
  CHECK(shellFirst.completionInstall);
  CHECK(flagFirst.completionInstall);
  CHECK_FALSE(shellFirst.completionUninstall);
  CHECK_FALSE(flagFirst.completionUninstall);
}

TEST_CASE(
  "completion install without a shell names the flag-first form",
  "[completion]"
) {
  TempDir temp;
  auto const errPath = temp.path / "stderr.txt";
  auto const parsed = testutils::parseArgs({"encro", "completion", "--install"});
  REQUIRE_FALSE(parsed.error.has_value());
  REQUIRE(parsed.completionInstall);
  REQUIRE(parsed.completionShell.empty());

  {
    auto capture = testutils::StderrCapture{errPath};
    CHECK(cmd::runCompletionCommand(parsed) == 1);
  }

  auto const errText = testutils::readTextFile(errPath);
  CHECK(
    errText.find("encro completion --install <powershell|bash>") != std::string::npos
  );
}

TEST_CASE("candidates channel prints template names one per line", "[completion]") {
  auto const temp = TempDir{};
  testutils::ScopedEnvVar const
    env{"ENCRO_CONFIG", pathutf8::fromPath(temp.path / "config.json")};
  auto const templates = temp.path / "templates";
  writeTemplateFile(templates, "webp.json");
  writeTemplateFile(templates, "anime.json");

  auto const run = runChannel("template-name");

  CHECK(run.exitCode == 0);
  CHECK(run.stdoutLines == std::vector<std::string>{"anime", "webp"});
}

TEST_CASE("candidates channel falls silent for an empty store", "[completion]") {
  auto const temp = TempDir{};
  testutils::ScopedEnvVar const
    env{"ENCRO_CONFIG", pathutf8::fromPath(temp.path / "config.json")};

  auto const run = runChannel("template-name");

  CHECK(run.exitCode == 0);
  CHECK(run.stdoutLines.empty());
}

TEST_CASE("candidates channel rejects an unknown slot", "[completion]") {
  auto const temp = TempDir{};
  testutils::ScopedEnvVar const
    env{"ENCRO_CONFIG", pathutf8::fromPath(temp.path / "config.json")};

  auto const run = runChannel("nosuch-slot");

  CHECK(run.exitCode == 1);
  CHECK(run.stderrText.find("nosuch-slot") != std::string::npos);
}

TEST_CASE(
  "candidates channel skips illegal and malformed template files",
  "[completion]"
) {
  auto const temp = TempDir{};
  testutils::ScopedEnvVar const
    env{"ENCRO_CONFIG", pathutf8::fromPath(temp.path / "config.json")};
  auto const templates = temp.path / "templates";
  writeTemplateFile(templates, "anime.json");
  writeTemplateFile(templates, "a b.json");  // stem the name rules reject
  writeTemplateFile(templates, "templates.schema.json");
  {
    auto broken =
      std::ofstream{templates / "broken.json", std::ios::binary | std::ios::trunc};
    broken << "{not json";
  }

  auto const run = runChannel("template-name");

  CHECK(run.exitCode == 0);
  // The illegal stem and the reserved schema file are not names, so they are
  // never offered; a malformed file's stem still is, because the channel lists
  // names and never parses content (selecting it reports the parse error).
  CHECK(run.stdoutLines == std::vector<std::string>{"anime", "broken"});
}

TEST_CASE("candidates channel ignores a corrupt config file", "[completion]") {
  auto const temp = TempDir{};
  auto const configPath = temp.path / "broken.json";
  {
    auto stream = std::ofstream{configPath, std::ios::binary | std::ios::trunc};
    stream << "{not json";
  }
  writeTemplateFile(temp.path / "templates", "anime.json");
  testutils::ScopedEnvVar const env{"ENCRO_CONFIG", pathutf8::fromPath(configPath)};

  auto const run = runChannel("template-name");

  CHECK(run.exitCode == 0);
  CHECK(run.stdoutLines == std::vector<std::string>{"anime"});
}

TEST_CASE("the candidate channel excludes the install actions", "[completion]") {
  auto const install = testutils::parseArgs(
    {"encro", "completion", "--candidates", "template-name", "--install"}
  );
  REQUIRE(install.error.has_value());
  CHECK(install.error->find("--install") != std::string::npos);
}
