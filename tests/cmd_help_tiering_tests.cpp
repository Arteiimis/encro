#include "cmd/cmd.h"
#include "infra/terminal.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ScopedColorNever {
  ScopedColorNever() { terminal::configure(terminal::ColorMode::Never); }
  ~ScopedColorNever() { terminal::reset(); }
};

constexpr auto kHintLine = std::string_view{"Run 'encro -hh' to view all options."};

bool isLastNonEmptyLine(std::string_view text, std::string_view needle) {
  auto const pos = text.find(needle);
  if (pos == std::string_view::npos) { return false; }

  auto const trailing = text.substr(pos + needle.size());
  return trailing.find_first_not_of(" \n\t\r") == std::string_view::npos;
}

}  // namespace

TEST_CASE(
  "brief help hides advanced options and ends with the -hh hint",
  "[cmd][tiering]"
) {
  auto const colorGuard = ScopedColorNever{};
  auto const result = testutils::parseArgs({"encro", "-h"});
  REQUIRE_FALSE(result.error.has_value());

  auto const& help = result.helpText;
  CHECK(help.find("--verbose") == std::string::npos);
  CHECK(help.find("--log-json") == std::string::npos);
  CHECK(help.find("--full-progress") == std::string::npos);
  CHECK(help.find("--color") == std::string::npos);
  CHECK(help.find("--inputs") == std::string::npos);
  CHECK(help.find("--state-file") == std::string::npos);
  CHECK(help.find("--force-conflict-handling") == std::string::npos);
  CHECK(help.find("--ffmpeg-path") == std::string::npos);
  CHECK(help.find("--preset") == std::string::npos);

  CHECK(isLastNonEmptyLine(help, kHintLine));

  CHECK(help.find("--input") != std::string::npos);
  CHECK(help.find("--crf") != std::string::npos);
}

TEST_CASE("full help via -hh shows advanced options and no hint", "[cmd][tiering]") {
  auto const colorGuard = ScopedColorNever{};
  auto const result = testutils::parseArgs({"encro", "-hh"});
  REQUIRE_FALSE(result.error.has_value());

  auto const& help = result.helpText;
  CHECK(help.find("--verbose") != std::string::npos);
  CHECK(help.find("--log-json") != std::string::npos);
  CHECK(help.find("--color") != std::string::npos);
  CHECK(help.find("--inputs") != std::string::npos);
  CHECK(help.find("--state-file") != std::string::npos);
  CHECK(help.find("--force-conflict-handling") != std::string::npos);
  CHECK(help.find("--ffmpeg-path") != std::string::npos);
  CHECK(help.find("--preset") != std::string::npos);

  CHECK(help.find(kHintLine) == std::string::npos);
}

TEST_CASE("full help via -h -h matches -hh", "[cmd][tiering]") {
  auto const colorGuard = ScopedColorNever{};
  auto const repeated = testutils::parseArgs({"encro", "-h", "-h"});
  auto const combined = testutils::parseArgs({"encro", "-hh"});

  REQUIRE_FALSE(repeated.error.has_value());
  REQUIRE_FALSE(combined.error.has_value());
  CHECK(repeated.helpText == combined.helpText);
}

TEST_CASE("parse-error help renders the brief tier", "[cmd][tiering]") {
  auto const colorGuard = ScopedColorNever{};
  auto const result = testutils::parseArgs({"encro", "--nope"});

  REQUIRE(result.error.has_value());
  auto const& help = result.helpText;
  CHECK(help.find("--verbose") == std::string::npos);
  CHECK(isLastNonEmptyLine(help, kHintLine));
}

TEST_CASE("help advertises the tiers in usage and -h description", "[cmd][tiering]") {
  auto const colorGuard = ScopedColorNever{};
  auto const result = testutils::parseArgs({"encro", "-hh"});
  REQUIRE_FALSE(result.error.has_value());

  auto const& help = result.helpText;
  CHECK(help.find("encro -h | -hh | --version") != std::string::npos);
  CHECK(help.find("show help; use -hh to show all options") != std::string::npos);
}

TEST_CASE("brief help keeps the usage section and all group headers", "[cmd][tiering]") {
  auto const colorGuard = ScopedColorNever{};
  auto const result = testutils::parseArgs({"encro", "-h"});
  REQUIRE_FALSE(result.error.has_value());

  auto const& help = result.helpText;
  auto const usagePos = help.find("Usage:");
  auto const generalPos = help.find("General options:");
  auto const ioPos = help.find("Input/Output options:");
  auto const processingPos = help.find("Processing options:");
  auto const fileopPos = help.find("File operation options:");

  CHECK(usagePos != std::string::npos);
  CHECK(usagePos < generalPos);
  CHECK(generalPos != std::string::npos);
  CHECK(ioPos != std::string::npos);
  CHECK(processingPos != std::string::npos);
  CHECK(fileopPos != std::string::npos);
  CHECK(help.find("encro -h | -hh | --version") != std::string::npos);
}

TEST_CASE("full help shows accurate option descriptions and defaults", "[cmd][tiering]") {
  auto const colorGuard = ScopedColorNever{};
  auto const result = testutils::parseArgs({"encro", "-hh"});
  REQUIRE_FALSE(result.error.has_value());

  auto const& help = result.helpText;

  auto const resumeLine =
    testutils::findHelpLine(help, "require matching previous job state");
  REQUIRE(resumeLine.has_value());
  CHECK(resumeLine->find("--resume") != std::string::npos);
  CHECK(resumeLine->find("error if missing or mismatched") != std::string::npos);

  auto const conflictLine = testutils::findHelpLine(help, "--force-conflict-handling");
  REQUIRE(conflictLine.has_value());
  CHECK(conflictLine->find("y=auto-rename, n=allow duplicates") != std::string::npos);
  CHECK(conflictLine->find("(=y)") != std::string::npos);

  auto const keepLine = testutils::findHelpLine(help, "--[no-]keep");
  REQUIRE(keepLine.has_value());
  CHECK(keepLine->find("preserve relative input subdirectories") != std::string::npos);
  CHECK(keepLine->find("(default:") != std::string::npos);
  CHECK(help.find("flatten)") != std::string::npos);

  auto const qualityLine = testutils::findHelpLine(help, "--image-quality");
  auto const crfLine = testutils::findHelpLine(help, "--crf");
  auto const presetLine = testutils::findHelpLine(help, "--preset");
  auto const jobsLine = testutils::findHelpLine(help, "--jobs");
  REQUIRE(qualityLine.has_value());
  REQUIRE(crfLine.has_value());
  REQUIRE(presetLine.has_value());
  REQUIRE(jobsLine.has_value());

  CHECK(qualityLine->find("(=2)") != std::string::npos);
  CHECK(crfLine->find("(=28)") != std::string::npos);
  CHECK(presetLine->find("(=auto)") != std::string::npos);
  CHECK(jobsLine->find("(=10)") != std::string::npos);

  for (
    auto const& line:
    {qualityLine.value(), crfLine.value(), presetLine.value(), jobsLine.value()}
  ) {
    CHECK(line.find("default=") == std::string::npos);
  }
}

TEST_CASE("brief help shows the crf default", "[cmd][tiering]") {
  auto const colorGuard = ScopedColorNever{};
  auto const result = testutils::parseArgs({"encro", "-h"});
  REQUIRE_FALSE(result.error.has_value());

  auto const crfLine = testutils::findHelpLine(result.helpText, "--crf");
  REQUIRE(crfLine.has_value());
  CHECK(crfLine->find("(=28)") != std::string::npos);
}
