#include "cmd/cmd.h"
#include "cmd/completion_command.h"

#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <fstream>
#include <string>
#include <string_view>

TEST_CASE("completion parse survives a corrupt config file", "[completion]") {
  TempDir temp;
  auto const configPath = temp.path / "broken.json";
  {
    auto stream = std::ofstream{configPath, std::ios::binary | std::ios::trunc};
    stream << "{not json";
  }
  // ScopedEnvVar restores the runner's pinned isolation path afterwards.
  testutils::ScopedEnvVar const envOverride{"ENCRO_CONFIG", configPath.string()};

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
