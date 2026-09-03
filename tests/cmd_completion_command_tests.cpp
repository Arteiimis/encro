#include "cmd/cmd.h"

#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <string>

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

TEST_CASE("bare completion shows the subcommand help", "[completion]") {
  auto const result = testutils::parseArgs({"encro", "completion"});
  REQUIRE_FALSE(result.error.has_value());
  CHECK(result.completion);
  CHECK(result.completionShell.empty());
  CHECK(result.helpText.find("encro completion <powershell|bash>") != std::string::npos);
  CHECK(
    result.helpText.find("print, install, or uninstall shell completion scripts")
    != std::string::npos
  );
}

TEST_CASE("completion -h routes through the help path", "[completion]") {
  auto const result = testutils::parseArgs({"encro", "completion", "-h"});
  CHECK(result.help);
  CHECK(result.helpText.find("encro completion <powershell|bash>") != std::string::npos);
}

TEST_CASE("main help commands section gains the completion row", "[completion]") {
  auto const result = testutils::parseArgs({"encro", "-h"});
  REQUIRE_FALSE(result.error.has_value());
  CHECK(
    result.helpText
      .find("completion   print, install, or uninstall shell completion scripts")
    != std::string::npos
  );
}
