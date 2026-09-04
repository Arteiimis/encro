#include "cmd/cmd.h"
#include "cmd/config_command.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <filesystem>
#include <string>
#include <vector>

// ── Config merge (design D1, tasks 3.2/3.3) ─────────────────────────────

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

TEST_CASE("config value applies when CLI omits the option", "[cmd][config]") {
  auto const config = ScopedConfigFile{"{\"crf\": 23, \"jobs\": 4}"};

  auto const result = testutils::parseArgs({"encro", "-i", "video.mp4"});
  REQUIRE_FALSE(result.error.has_value());
  CHECK(result.crf == 23);
  CHECK(result.maxJobs == 4);
}

TEST_CASE("cli value beats config value", "[cmd][config]") {
  auto const config = ScopedConfigFile{"{\"crf\": 23}"};

  SECTION("main position") {
    auto const result = testutils::parseArgs({"encro", "--crf", "30"});
    REQUIRE_FALSE(result.error.has_value());
    CHECK(result.crf == 30);
  }

  SECTION("explicit value before the preview subcommand") {
    auto const result =
      testutils::parseArgs({"encro", "--crf", "20", "preview", "clip.mp4"});
    REQUIRE_FALSE(result.error.has_value());
    CHECK(result.crf == 20);
  }

  SECTION("explicit value inside the preview subcommand") {
    auto const result =
      testutils::parseArgs({"encro", "preview", "--crf", "20", "clip.mp4"});
    REQUIRE_FALSE(result.error.has_value());
    CHECK(result.crf == 20);
  }
}

TEST_CASE(
  "stored image-quality runs without --compress (config does not trip needs)",
  "[cmd][config]"
) {
  auto const config = ScopedConfigFile{"{\"image-quality\": 10}"};

  auto const result = testutils::parseArgs({"encro", "-i", "video.mp4"});
  REQUIRE_FALSE(result.error.has_value());
  REQUIRE(result.imageQuality.has_value());
  CHECK(result.imageQuality.value() == 10);
}

TEST_CASE("invalid stored value fails the run when effective", "[cmd][config]") {
  auto const config = ScopedConfigFile{"{\"crf\": 99}"};

  auto const result = testutils::parseArgs({"encro", "-i", "video.mp4"});
  REQUIRE(result.error.has_value());
  CHECK(result.error->find("crf") != std::string::npos);
}

TEST_CASE("invalid stored value unused when cli overrides the option", "[cmd][config]") {
  auto const config = ScopedConfigFile{"{\"crf\": 99}"};

  auto const result = testutils::parseArgs({"encro", "--crf", "20"});
  REQUIRE_FALSE(result.error.has_value());
  CHECK(result.crf == 20);
}

TEST_CASE(
  "invalid stored value fails even with -h, keeping the store loud",
  "[cmd][config]"
) {
  auto const config = ScopedConfigFile{"{\"crf\": 99}"};

  auto const result = testutils::parseArgs({"encro", "-h"});
  REQUIRE(result.error.has_value());
}

TEST_CASE("unknown config key warns but the run proceeds", "[cmd][config]") {
  auto const config = ScopedConfigFile{"{\"dry-run\": true}"};

  auto const result = testutils::parseArgs({"encro", "-i", "video.mp4"});
  REQUIRE_FALSE(result.error.has_value());
  CHECK_FALSE(result.dryRun);
}

TEST_CASE("malformed config file fails the parse naming the file", "[cmd][config]") {
  auto const config = ScopedConfigFile{"{ broken"};

  auto const result = testutils::parseArgs({"encro", "-i", "video.mp4"});
  REQUIRE(result.error.has_value());
  CHECK(result.error->find(config.path().string()) != std::string::npos);
}

TEST_CASE("missing config file leaves behavior untouched", "[cmd][config]") {
  auto const config = ScopedConfigFile{""};
  std::filesystem::remove(config.path());

  auto const result = testutils::parseArgs({"encro", "-i", "video.mp4"});
  REQUIRE_FALSE(result.error.has_value());
  CHECK_FALSE(result.crf.has_value());
}

// ── Negation flags (task 4.1) ────────────────────────────────────────────

TEST_CASE("negation flags override persisted true values", "[cmd][config]") {
  auto const config = ScopedConfigFile{
    R"({"pack": true, "keep": true, "compress": true, "recursive": true, "folder-summary": true, "yes": true})"
  };

  SECTION("absent forms apply the persisted values") {
    auto const result = testutils::parseArgs({"encro", "-i", "video.mp4"});
    REQUIRE_FALSE(result.error.has_value());
    CHECK(result.pack);
    CHECK(result.keep);
    CHECK(result.compress);
    CHECK(result.recursive);
    CHECK(result.folderSummary);
    CHECK(result.yesToAll);
  }

  SECTION("negation forms override them") {
    auto const result = testutils::parseArgs(
      {"encro",
       "--no-pack",
       "--no-keep",
       "--no-compress",
       "--no-recursive",
       "--no-folder-summary",
       "--no-yes"}
    );
    REQUIRE_FALSE(result.error.has_value());
    CHECK_FALSE(result.pack);
    CHECK_FALSE(result.keep);
    CHECK_FALSE(result.compress);
    CHECK_FALSE(result.recursive);
    CHECK_FALSE(result.folderSummary);
    CHECK_FALSE(result.yesToAll);
  }
}

// ── Help rendering (tasks 4.2/4.3) ────────────────────────────────────────

TEST_CASE(
  "brief help shows the config commands row and collapsed negation names",
  "[cmd][config]"
) {
  auto const result = testutils::parseArgs({"encro", "-h"});
  REQUIRE_FALSE(result.error.has_value());

  auto const& help = result.helpText;
  // Negation flags render collapsed; bare --no-* names stay out of the help.
  CHECK(help.find("-p, --[no-]pack") != std::string::npos);
  CHECK(help.find("-y, --[no-]yes") != std::string::npos);
  CHECK(testutils::findHelpLine(help, "--no-pack") == std::nullopt);
  CHECK(testutils::findHelpLine(help, "--no-yes") == std::nullopt);
}

TEST_CASE(
  "help default display shows config-adjusted effective defaults",
  "[cmd][config]"
) {
  auto const config = ScopedConfigFile{"{\"crf\": 23}"};

  auto const result = testutils::parseArgs({"encro", "-h"});
  REQUIRE_FALSE(result.error.has_value());
  auto const crfLine = testutils::findHelpLine(result.helpText, "--crf");
  REQUIRE(crfLine.has_value());
  CHECK(crfLine->find("(=23)") != std::string::npos);
}

TEST_CASE(
  "preview help default display shows config-adjusted effective defaults too",
  "[cmd][config]"
) {
  auto const config = ScopedConfigFile{"{\"crf\": 23, \"preset\": \"p5\"}"};

  auto const result = testutils::parseArgs({"encro", "preview", "-h"});
  REQUIRE_FALSE(result.error.has_value());
  auto const crfLine = testutils::findHelpLine(result.helpText, "--crf");
  REQUIRE(crfLine.has_value());
  CHECK(crfLine->find("(=23)") != std::string::npos);
  auto const presetLine = testutils::findHelpLine(result.helpText, "--preset");
  REQUIRE(presetLine.has_value());
  CHECK(presetLine->find("(=p5)") != std::string::npos);
}

// ── Config subcommand parsing (task 5.1) ─────────────────────────────────

TEST_CASE("config subcommand actions parse and exclude each other", "[cmd][config]") {
  SECTION("each action parses") {
    CHECK(testutils::parseArgs({"encro", "config", "--list"}).configList);

    auto const getPath = testutils::parseArgs({"encro", "config", "--get", "crf"});
    CHECK(getPath.configGet == "crf");

    auto const setPath = testutils::parseArgs({"encro", "config", "--set", "crf", "20"});
    REQUIRE(setPath.configSet.has_value());
    CHECK(setPath.configSet->size() == 2);
    CHECK(setPath.configSet.value()[0] == "crf");
    CHECK(setPath.configSet.value()[1] == "20");

    CHECK(
      testutils::parseArgs({"encro", "config", "--unset", "crf"}).configUnset == "crf"
    );
    CHECK(testutils::parseArgs({"encro", "config", "--path"}).configPath);
  }

  SECTION("combined actions are rejected natively") {
    auto const result = testutils::parseArgs({"encro", "config", "--list", "--path"});
    CHECK(result.error.has_value());
  }

  SECTION("set requires exactly two values") {
    CHECK(testutils::parseArgs({"encro", "config", "--set", "crf"}).error.has_value());
    CHECK(
      testutils::parseArgs({"encro", "config", "--set", "crf", "20", "30"})
        .error.has_value()
    );
  }

  SECTION("bare config carries the config help") {
    auto const result = testutils::parseArgs({"encro", "config"});
    REQUIRE(result.config);
    CHECK_FALSE(result.configList);
    CHECK(result.helpText.find("--set") != std::string::npos);
    CHECK(result.helpText.find("--unset") != std::string::npos);
  }

  SECTION("config -h prints the config help with success") {
    auto const result = testutils::parseArgs({"encro", "config", "-h"});
    CHECK(result.help);
    CHECK(result.helpText.find("--unset") != std::string::npos);
    CHECK(result.helpText.find("--verbose") == std::string::npos);  // not the main help
  }

  SECTION("unknown action fails natively") {
    CHECK(testutils::parseArgs({"encro", "config", "--export"}).error.has_value());
  }

  SECTION("subcommand name wins over positional input") {
    auto const result = testutils::parseArgs({"encro", "config"});
    CHECK(result.config);
    CHECK_FALSE(result.positionalInputs.has_value());
  }
}

// ── Config command execution (task 5.3) ─────────────────────────────

TEST_CASE(
  "config set persists a validated value and unset restores the default",
  "[cmd][config]"
) {
  auto const parsed =
    testutils::parseArgs({"encro", "config", "--list"});  // populate the registry
  REQUIRE_FALSE(parsed.error.has_value());

  auto const temp = TempDir{};
  auto const configPath = temp.path / "nested" / "config.json";
  auto const guard = testutils::ScopedEnvVar("ENCRO_CONFIG", configPath.string());

  SECTION("set writes the file, creating parent directories") {
    auto setResult = testutils::parseArgs({"encro", "config", "--set", "crf", "20"});
    CHECK_FALSE(setResult.error.has_value());
    CHECK(cmd::runConfigCommand(setResult) == 0);

    auto const text = testutils::readTextFile(configPath);
    CHECK(text.find("\"crf\": 20") != std::string::npos);
  }

  SECTION("set rejects unknown keys and invalid values without writing") {
    auto unknown = testutils::parseArgs({"encro", "config", "--set", "dry-run", "true"});
    CHECK(cmd::runConfigCommand(unknown) == 1);

    auto invalid = testutils::parseArgs({"encro", "config", "--set", "crf", "99"});
    CHECK(cmd::runConfigCommand(invalid) == 1);

    CHECK_FALSE(std::filesystem::exists(configPath));
  }

  SECTION("set rejects non-boolean values for flag keys") {
    auto notBoolean =
      testutils::parseArgs({"encro", "config", "--set", "pack", "banana"});
    CHECK(cmd::runConfigCommand(notBoolean) == 1);
    CHECK_FALSE(std::filesystem::exists(configPath));
  }

  SECTION("set rejects non-integer values for number keys") {
    auto notInteger = testutils::parseArgs({"encro", "config", "--set", "crf", "4.5"});
    CHECK(cmd::runConfigCommand(notInteger) == 1);

    auto inRange = testutils::parseArgs({"encro", "config", "--set", "jobs", "4.5"});
    CHECK(cmd::runConfigCommand(inRange) == 1);

    CHECK_FALSE(std::filesystem::exists(configPath));
  }

  SECTION("set accepts boolean true and false for flag keys") {
    auto on = testutils::parseArgs({"encro", "config", "--set", "pack", "true"});
    CHECK(cmd::runConfigCommand(on) == 0);
    auto off = testutils::parseArgs({"encro", "config", "--set", "pack", "false"});
    CHECK(cmd::runConfigCommand(off) == 0);
    CHECK(
      testutils::readTextFile(configPath).find("\"pack\": false") != std::string::npos
    );
  }

  SECTION("set canonicalizes transformed values") {
    auto setResult =
      testutils::parseArgs({"encro", "config", "--set", "force-conflict-handling", "N"});
    CHECK(cmd::runConfigCommand(setResult) == 0);
    CHECK(
      testutils::readTextFile(configPath).find("\"force-conflict-handling\": \"n\"")
      != std::string::npos
    );
  }

  SECTION("get and list reflect the store") {
    auto setResult = testutils::parseArgs({"encro", "config", "--set", "crf", "20"});
    REQUIRE(cmd::runConfigCommand(setResult) == 0);

    {
      auto capture = testutils::StdoutCapture{temp.path / "stdout.txt"};
      auto getResult = testutils::parseArgs({"encro", "config", "--get", "crf"});
      CHECK(cmd::runConfigCommand(getResult) == 0);
    }
    CHECK(
      testutils::readTextFile(temp.path / "stdout.txt").find("20") != std::string::npos
    );
  }

  SECTION("unset removes the key and falls back to the default") {
    REQUIRE(
      cmd::runConfigCommand(
        testutils::parseArgs({"encro", "config", "--set", "jobs", "4"})
      )
      == 0
    );
    REQUIRE(
      cmd::runConfigCommand(testutils::parseArgs({"encro", "config", "--unset", "jobs"}))
      == 0
    );
    CHECK_FALSE(testutils::readTextFile(configPath).find("jobs") != std::string::npos);
    CHECK(
      cmd::runConfigCommand(testutils::parseArgs({"encro", "config", "--unset", "jobs"}))
      == 0
    );
  }

  SECTION("path prints the resolved location without reading the file") {
    testutils::writeTextFile(configPath, "{ broken");

    {
      auto capture = testutils::StdoutCapture{temp.path / "stdout-path.txt"};
      auto pathResult = testutils::parseArgs({"encro", "config", "--path"});
      CHECK(cmd::runConfigCommand(pathResult) == 0);
    }
    CHECK(
      testutils::readTextFile(temp.path / "stdout-path.txt").find(configPath.string())
      != std::string::npos
    );
  }

  SECTION("actions fail on a malformed store") {
    testutils::writeTextFile(configPath, "{ broken");
    CHECK(
      cmd::runConfigCommand(testutils::parseArgs({"encro", "config", "--list"})) == 1
    );
    CHECK(
      cmd::runConfigCommand(testutils::parseArgs({"encro", "config", "--get", "crf"}))
      == 1
    );
    CHECK(
      cmd::runConfigCommand(
        testutils::parseArgs({"encro", "config", "--set", "crf", "20"})
      )
      == 1
    );
    CHECK(
      cmd::runConfigCommand(testutils::parseArgs({"encro", "config", "--unset", "crf"}))
      == 1
    );
    CHECK(
      cmd::runConfigCommand(testutils::parseArgs({"encro", "config", "--path"})) == 0
    );
  }
}
