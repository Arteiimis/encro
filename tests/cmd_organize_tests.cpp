// organize subcommand surface: parse defaults, help privacy line, and the
// model-dir config key (task 5.1).
#include "cmd/cmd.h"
#include "cmd/config_command.h"

#include "test_utils.h"

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct ScopedConfigFile {
  TempDir temp;
  testutils::ScopedEnvVar guard;

  explicit ScopedConfigFile(std::string_view content)
    : temp(), guard("ENCRO_CONFIG", write(content).string()) { }

  auto path() const -> fs::path { return temp.path / "config.json"; }
  auto write(std::string_view content) const -> fs::path {
    return testutils::writeTextFile(path(), content);
  }
};

}  // namespace

TEST_CASE("organize subcommand parses dir and defaults", "[cmd][organize]") {
  auto const result = testutils::parseArgs({"encro", "organize", "D:/pics"});
  REQUIRE_FALSE(result.error.has_value());
  CHECK(result.organize);
  REQUIRE(result.organizeDir.has_value());
  CHECK(result.organizeDir.value() == "D:/pics");
  // Threshold default is applied at the command layer (value_or); the
  // binding only carries explicitly passed values, like the parent options.
  CHECK_FALSE(result.organizeMinConfidence.has_value());
  CHECK_FALSE(result.organizeDownloadModels);
  CHECK_FALSE(result.organizeRecluster);
  CHECK_FALSE(result.dryRun);
}

TEST_CASE("organize flags and threshold parse", "[cmd][organize]") {
  auto const result = testutils::parseArgs({
    "encro",
    "organize",
    "D:/pics",
    "-r",
    "--min-confidence",
    "0.5",
    "--download-models",
    "--dry-run",
    "--recluster",
  });
  REQUIRE_FALSE(result.error.has_value());
  CHECK(result.recursive);
  REQUIRE(result.organizeMinConfidence.has_value());
  CHECK(result.organizeMinConfidence.value() == Catch::Approx(0.5));
  CHECK(result.organizeDownloadModels);
  CHECK(result.dryRun);
  CHECK(result.organizeRecluster);
}

TEST_CASE("organize help states the local-only privacy line", "[cmd][organize]") {
  auto const result = testutils::parseArgs({"encro", "organize", "-h"});
  CHECK(result.help);
  CHECK(result.helpText().find("never leaves this machine") != std::string::npos);
  CHECK(result.helpText().find("--download-models") != std::string::npos);
}

TEST_CASE("main help lists organize", "[cmd][organize]") {
  auto const result = testutils::parseArgs({"encro", "-h"});
  CHECK(result.helpText().find("organize") != std::string::npos);
}

TEST_CASE("model-dir config key set/get round-trips", "[cmd][organize][config]") {
  auto configFile = ScopedConfigFile{"{}"};

  auto const setExit = cmd::runConfigCommand(
    testutils::parseArgs({
      "encro",
      "config",
      "set",
      "model-dir",
      "D:/models",
    })
  );
  CHECK(setExit == 0);

  auto const getExit = cmd::runConfigCommand(
    testutils::parseArgs({
      "encro",
      "config",
      "get",
      "model-dir",
    })
  );
  CHECK(getExit == 0);
}

TEST_CASE(
  "model-dir config value reaches the organize subcommand",
  "[cmd][organize][config]"
) {
  auto configFile = ScopedConfigFile{R"({"model-dir": "D:/models"})"};
  auto const result = testutils::parseArgs({"encro", "organize", "D:/pics"});
  REQUIRE_FALSE(result.error.has_value());
  REQUIRE(result.organizeModelDir.has_value());
  CHECK(result.organizeModelDir.value() == "D:/models");
}
