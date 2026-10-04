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

// --identity-tau: the calibrated threshold, exposed because it is an operating
// point rather than a derived number (design D7/D8).
TEST_CASE("identity-tau parses and is validated", "[cmd][organize]") {
  auto const plain = testutils::parseArgs({"encro", "organize", "D:/pics"});
  REQUIRE_FALSE(plain.error.has_value());
  CHECK_FALSE(plain.organizeIdentityTau.has_value());

  auto const flagged =
    testutils::parseArgs({"encro", "organize", "D:/pics", "--identity-tau", "0.72"});
  REQUIRE_FALSE(flagged.error.has_value());
  REQUIRE(flagged.organizeIdentityTau.has_value());
  CHECK(flagged.organizeIdentityTau.value() == Catch::Approx(0.72));

  // Above 1 is not a cosine threshold at all, and 0 would join everything.
  auto const outOfRange =
    testutils::parseArgs({"encro", "organize", "D:/pics", "--identity-tau", "1.5"});
  REQUIRE(outOfRange.error.has_value());
  CHECK(outOfRange.error->find("identity-tau") != std::string::npos);

  auto const zero =
    testutils::parseArgs({"encro", "organize", "D:/pics", "--identity-tau", "0"});
  REQUIRE(zero.error.has_value());
}

// The help is the only place a user learns which way to turn the knob, so the
// wording is pinned separately from the parsing contract.
TEST_CASE("identity-tau is documented in the organize help", "[cmd][organize]") {
  auto const help = testutils::parseArgs({"encro", "organize", "-h"});
  CHECK(help.helpText().find("--identity-tau") != std::string::npos);
  CHECK(help.helpText().find("default 0.70") != std::string::npos);
  CHECK(help.helpText().find("look-alikes apart") != std::string::npos);
}

TEST_CASE(
  "identity-tau config value applies and the flag wins over it",
  "[cmd][organize][config]"
) {
  auto configFile = ScopedConfigFile{"{"
                                     "}"};

  // The store rejects a registered key that is missing from its canonical
  // order, so a set/get round-trip is what proves the key is wired.
  auto const setExit = cmd::runConfigCommand(
    testutils::parseArgs({"encro", "config", "set", "identity-tau", "0.62"})
  );
  CHECK(setExit == 0);
  auto const getExit = cmd::runConfigCommand(
    testutils::parseArgs({"encro", "config", "get", "identity-tau"})
  );
  CHECK(getExit == 0);

  auto const fromConfig = testutils::parseArgs({"encro", "organize", "D:/pics"});
  REQUIRE_FALSE(fromConfig.error.has_value());
  REQUIRE(fromConfig.organizeIdentityTau.has_value());
  CHECK(fromConfig.organizeIdentityTau.value() == Catch::Approx(0.62));

  auto const fromFlag =
    testutils::parseArgs({"encro", "organize", "D:/pics", "--identity-tau", "0.72"});
  REQUIRE(fromFlag.organizeIdentityTau.has_value());
  CHECK(fromFlag.organizeIdentityTau.value() == Catch::Approx(0.72));

  // The store validates with the option's own copied rules, so the lower bound
  // the CLI enforces also rejects a stored zero.
  auto const zeroExit = cmd::runConfigCommand(
    testutils::parseArgs({"encro", "config", "set", "identity-tau", "0"})
  );
  CHECK(zeroExit != 0);
}

// --download-models is a complete request on its own: fetching the models
// needs no images, so the directory is optional with it.
TEST_CASE("organize parses --download-models without a directory", "[cmd][organize]") {
  auto const result = testutils::parseArgs({"encro", "organize", "--download-models"});
  CHECK_FALSE(result.error.has_value());
  CHECK(result.organize);
  CHECK(result.organizeDownloadModels);
  CHECK_FALSE(result.organizeDir.has_value());
}

TEST_CASE("organize without a directory asks for one", "[cmd][organize]") {
  auto const result = testutils::parseArgs({"encro", "organize"});
  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("dir is required") != std::string::npos);
}

TEST_CASE("organize help shows the directory as optional", "[cmd][organize]") {
  // Pinned width so the flag's description cannot wrap mid-phrase.
  auto const columnsVar = testutils::ScopedEnvVar{"COLUMNS", "120"};
  auto const result = testutils::parseArgs({"encro", "organize", "-h"});
  CHECK(result.help);
  CHECK(result.helpText().find("encro organize [dir]") != std::string::npos);
  CHECK(result.helpText().find("no dir: fetch only") != std::string::npos);
}

// --ingest / --ignore-folder: repeatable, one name per occurrence (change
// organize-existing-structure task 2.1).
TEST_CASE("ingest and ignore-folder flags parse repeatable", "[cmd][organize]") {
  auto temp = TempDir{};
  fs::create_directories(temp.path / "mix");
  fs::create_directories(temp.path / "角色A");
  fs::create_directories(temp.path / ".stash");

  auto const result = testutils::parseArgs({
    "encro",
    "organize",
    temp.path.string(),
    "--ingest",
    "mix",
    "--ingest",
    "角色A",
    "--ignore-folder",
    ".stash",
  });
  REQUIRE_FALSE(result.error.has_value());
  REQUIRE(result.organizeIngest.has_value());
  CHECK(*result.organizeIngest == std::vector<std::string>{"mix", "角色A"});
  REQUIRE(result.organizeIgnoreFolder.has_value());
  CHECK(*result.organizeIgnoreFolder == std::vector<std::string>{".stash"});
}

TEST_CASE(
  "an ingest name that is not a first-level folder is an argument error",
  "[cmd][organize]"
) {
  auto temp = TempDir{};
  fs::create_directories(temp.path / "mix");
  fs::create_directories(temp.path / "organized" / "miku");

  auto const unknown = testutils::parseArgs(
    {"encro", "organize", temp.path.string(), "--ingest", "nonexistent"}
  );
  REQUIRE(unknown.error.has_value());
  CHECK(unknown.error->find("--ingest") != std::string::npos);
  CHECK(unknown.error->find("nonexistent") != std::string::npos);

  // The output tree is not an ingestable folder either.
  auto const output = testutils::parseArgs(
    {"encro", "organize", temp.path.string(), "--ingest", "organized"}
  );
  CHECK(output.error.has_value());
}

TEST_CASE("the same folder in both flags is an argument error", "[cmd][organize]") {
  auto temp = TempDir{};
  fs::create_directories(temp.path / "mix");

  auto const result = testutils::parseArgs({
    "encro",
    "organize",
    temp.path.string(),
    "--ingest",
    "mix",
    "--ignore-folder",
    "mix",
  });
  REQUIRE(result.error.has_value());
  CHECK(result.error->find("mix") != std::string::npos);
}

TEST_CASE("ingest parses with the directory before the flag", "[cmd][organize]") {
  auto temp = TempDir{};
  fs::create_directories(temp.path / "mix");

  auto const result =
    testutils::parseArgs({"encro", "organize", temp.path.string(), "--ingest", "mix"});
  REQUIRE_FALSE(result.error.has_value());
  REQUIRE(result.organizeDir.has_value());
  CHECK(*result.organizeDir == temp.path.string());
  REQUIRE(result.organizeIngest.has_value());
  CHECK(*result.organizeIngest == std::vector<std::string>{"mix"});
}

TEST_CASE("organize help lists the disposition flags", "[cmd][organize]") {
  auto const columnsVar = testutils::ScopedEnvVar{"COLUMNS", "120"};
  auto const result = testutils::parseArgs({"encro", "organize", "-h"});
  CHECK(result.helpText().find("--ingest") != std::string::npos);
  CHECK(result.helpText().find("--ignore-folder") != std::string::npos);
}
