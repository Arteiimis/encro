#include "cmd/config_store.h"
#include "test_utils.h"

#include <boost/json.hpp>        // IWYU pragma: keep

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

// ScopedEnvVar with "" unsets on Windows (CRT semantics), which is exactly
// what the fallback tests need.
struct ConfigEnvScope {
  explicit ConfigEnvScope(fs::path const& path): guard_("ENCRO_CONFIG", path.string()) { }
  ConfigEnvScope(std::string_view key, std::string_view value)
    : guard_(std::string{key}, std::string{value}) { }

  testutils::ScopedEnvVar guard_;
};

// The real assembled table: only a real registration pass (parseArgs builds the
// whole app tree) produces one. Immutable, so one shared instance serves every
// case in this file.
auto realTable() -> configstore::KeyTable const& {
  static auto const table = testutils::parseArgs({"encro"}).keyTable;
  return table;
}

// A registration-time entry draft for the assembler cases: only the key is
// load-bearing for ordering.
auto draftKey(
  std::string_view key,
  configstore::JsonKind kind = configstore::JsonKind::String
) -> configstore::KeyDef {
  return {.key = key, .kind = kind};
}

}  // namespace

TEST_CASE("config path resolution honors ENCRO_CONFIG", "[cmd][config-store]") {
  auto const temp = TempDir{};
  auto const path = temp.path / "my-config.json";
  auto const guard = ConfigEnvScope(path);

  CHECK(configstore::resolveConfigPath() == path);
}

TEST_CASE(
  "config path resolution falls back to the platform config root",
  "[cmd][config-store]"
) {
  auto const temp = TempDir{};

#if defined(_WIN32)
  auto const unsetLocal = ConfigEnvScope{"LOCALAPPDATA", ""};
  auto const unsetEncro = ConfigEnvScope{"ENCRO_CONFIG", ""};
  auto const appData = fs::temp_directory_path() / "encro-fake-appdata";
  auto const roaming = ConfigEnvScope{"APPDATA", appData.string()};
  CHECK(configstore::resolveConfigPath() == appData / "encro" / "config.json");

  auto const localData = fs::temp_directory_path() / "encro-fake-localappdata";
  auto const local = ConfigEnvScope{"LOCALAPPDATA", localData.string()};
  CHECK(configstore::resolveConfigPath() == localData / "encro" / "config.json");
#else
  auto const unsetHome = ConfigEnvScope{"HOME", ""};
  auto const unsetEncro = ConfigEnvScope{"ENCRO_CONFIG", ""};
  auto const configHome = temp.path / "xdg-config";
  auto const xdg = ConfigEnvScope{"XDG_CONFIG_HOME", configHome.string()};
  CHECK(configstore::resolveConfigPath() == configHome / "encro" / "config.json");

  auto const home = temp.path / "home";
  auto const homeGuard = ConfigEnvScope{"HOME", home.string()};
  auto const unsetXdg = ConfigEnvScope{"XDG_CONFIG_HOME", ""};
  CHECK(configstore::resolveConfigPath() == home / ".config" / "encro" / "config.json");
#endif
}

TEST_CASE("loading a missing config file yields an empty store", "[cmd][config-store]") {
  auto const temp = TempDir{};
  auto const guard = ConfigEnvScope(temp.path / "absent.json");

  auto const loaded = configstore::load(configstore::resolveConfigPath(), realTable());
  REQUIRE_FALSE(loaded.error.has_value());
  CHECK(loaded.values.empty());
  CHECK(loaded.unknownKeys.empty());
}

TEST_CASE(
  "loading a malformed config file names the file and parse issue",
  "[cmd][config-store]"
) {
  auto const temp = TempDir{};
  auto const path = temp.path / "broken.json";
  testutils::writeTextFile(path, "{ not json !!!");
  auto const guard = ConfigEnvScope(path);

  auto const loaded = configstore::load(path, realTable());
  REQUIRE(loaded.error.has_value());
  CHECK(loaded.error->find(path.string()) != std::string::npos);
}

TEST_CASE("loading a non-object config file fails", "[cmd][config-store]") {
  auto const temp = TempDir{};
  auto const path = temp.path / "array.json";
  testutils::writeTextFile(path, "[1, 2, 3]");
  auto const guard = ConfigEnvScope(path);

  auto const loaded = configstore::load(path, realTable());
  REQUIRE(loaded.error.has_value());
  CHECK(loaded.error->find("object") != std::string::npos);
}

TEST_CASE("loading stringifies scalars and reports unknown keys", "[cmd][config-store]") {
  auto const temp = TempDir{};
  auto const path = temp.path / "config.json";
  testutils::writeTextFile(
    path,
    R"({"crf": 23, "pack": true, "ffmpeg-path": "D:/tools", "dry-run": false})"
  );
  auto const guard = ConfigEnvScope(path);

  auto const loaded = configstore::load(path, realTable());
  REQUIRE_FALSE(loaded.error.has_value());
  CHECK(loaded.values.size() == 3);
  CHECK(loaded.values.at("crf") == "23");
  CHECK(loaded.values.at("pack") == "true");
  CHECK(loaded.values.at("ffmpeg-path") == "D:/tools");
  REQUIRE(loaded.unknownKeys.size() == 1);
  CHECK(loaded.unknownKeys.front() == "dry-run");
}

TEST_CASE(
  "loading a non-scalar value for a known key fails naming the key",
  "[cmd][config-store]"
) {
  auto const temp = TempDir{};
  auto const path = temp.path / "config.json";
  testutils::writeTextFile(path, R"({"jobs": [4]})");
  auto const guard = ConfigEnvScope(path);

  auto const loaded = configstore::load(path, realTable());
  REQUIRE(loaded.error.has_value());
  CHECK(loaded.error->find("jobs") != std::string::npos);
}

TEST_CASE("assembleKeyTable rejects a token with no order entry", "[cmd][config-store]") {
  auto const entries = std::array{draftKey("crf"), draftKey("extra")};
  auto const order = std::array<std::string_view, 1>{"crf"};

  auto const table = configstore::assembleKeyTable(order, entries);
  REQUIRE_FALSE(table.has_value());
  CHECK(table.error().find("extra") != std::string::npos);
}

TEST_CASE(
  "assembleKeyTable rejects an order entry no token registers",
  "[cmd][config-store]"
) {
  auto const entries = std::array{draftKey("crf")};
  auto const order = std::array<std::string_view, 2>{"crf", "jobs"};

  auto const table = configstore::assembleKeyTable(order, entries);
  REQUIRE_FALSE(table.has_value());
  CHECK(table.error().find("jobs") != std::string::npos);
}

TEST_CASE("assembleKeyTable rejects a duplicate key", "[cmd][config-store]") {
  auto const entries = std::array{draftKey("crf"), draftKey("crf")};
  auto const order = std::array<std::string_view, 1>{"crf"};

  auto const table = configstore::assembleKeyTable(order, entries);
  REQUIRE_FALSE(table.has_value());
  CHECK(table.error().find("crf") != std::string::npos);
}

TEST_CASE(
  "assembleKeyTable yields the entries in canonical order",
  "[cmd][config-store]"
) {
  auto const entries =
    std::array{draftKey("jobs", configstore::JsonKind::Number), draftKey("color")};
  auto const order = std::array<std::string_view, 2>{"color", "jobs"};

  auto const table = configstore::assembleKeyTable(order, entries);
  REQUIRE(table.has_value());
  REQUIRE(table->keys.size() == 2);
  CHECK(table->keys[0].key == "color");
  CHECK(table->keys[1].key == "jobs");
  CHECK(table->keys[1].kind == configstore::JsonKind::Number);
}

TEST_CASE(
  "the assembled table derives each key's JSON kind from its binding",
  "[cmd][config-store]"
) {
  auto const& table = realTable();

  auto const* yes = table.find("yes");  // bool flag
  REQUIRE(yes != nullptr);
  CHECK(yes->kind == configstore::JsonKind::Boolean);

  auto const* crf = table.find("crf");  // std::optional<int>
  REQUIRE(crf != nullptr);
  CHECK(crf->kind == configstore::JsonKind::Number);

  auto const* color = table.find("color");  // std::string
  REQUIRE(color != nullptr);
  CHECK(color->kind == configstore::JsonKind::String);

  // Token-carrying specs record an entry wherever they sit (the organize
  // subcommand included); a token-less spec such as --dry-run records none.
  CHECK(table.find("jobs") != nullptr);
  CHECK(table.find("model-dir") != nullptr);
  CHECK(table.find("dry-run") == nullptr);
}

TEST_CASE("save writes pretty canonical form and round-trips", "[cmd][config-store]") {
  auto const temp = TempDir{};
  auto const path = temp.path / "nested" / "config.json";

  auto values = std::map<std::string, std::string>{};
  values["crf"] = "23";
  values["jobs"] = "4";
  values["pack"] = "true";
  values["preset"] = "p5";

  REQUIRE_FALSE(configstore::save(path, values, realTable()).has_value());

  auto const text = testutils::readTextFile(path);
  CHECK(text.starts_with("{\n"));
  CHECK(text.find("\n    ") != std::string::npos);          // indented keys
  CHECK(text.find("\"crf\": 23") != std::string::npos);     // native number
  CHECK(text.find("\"pack\": true") != std::string::npos);  // native boolean
  CHECK(text.find("\"jobs\"") < text.find("\"crf\""));      // canonical order

  auto const guard = ConfigEnvScope(path);
  auto const loaded = configstore::load(path, realTable());
  REQUIRE_FALSE(loaded.error.has_value());
  CHECK(loaded.values == values);
}

TEST_CASE("save escapes string values", "[cmd][config-store]") {
  auto const temp = TempDir{};
  auto const path = temp.path / "config.json";

  auto values = std::map<std::string, std::string>{};
  values["ffmpeg-path"] = "D:\\tools\\ffmpeg";

  REQUIRE_FALSE(configstore::save(path, values, realTable()).has_value());

  auto const text = testutils::readTextFile(path);
  CHECK(text.find("\"ffmpeg-path\": \"D:\\\\tools\\\\ffmpeg\"") != std::string::npos);
}

TEST_CASE("save of an empty store writes an empty JSON object", "[cmd][config-store]") {
  auto const temp = TempDir{};
  auto const path = temp.path / "config.json";

  REQUIRE_FALSE(configstore::save(path, {}, realTable()).has_value());
  CHECK(testutils::readTextFile(path) == "{\n}\n");
}

TEST_CASE("table validate runs the copied validators in order", "[cmd][config-store]") {
  // A synthetic entry shaped like registration captures `--crf`: the transform
  // sits before the check, because CLI11 inserts transforms at the front of
  // the option's validator list.
  SECTION("a synthetic transform-then-check entry") {
    auto const entries = std::array{
      configstore::KeyDef{
        .key = "crf",
        .kind = configstore::JsonKind::Number,
        .validators =
          {CLI::CheckedTransformer{
             std::vector<std::pair<std::string, std::string>>{
               {"low", "0"},
               {"high", "51"}
             },
           },
           CLI::Range(0, 51)},
      },
    };
    auto const order = std::array<std::string_view, 1>{"crf"};
    auto const table = configstore::assembleKeyTable(order, entries);
    REQUIRE(table.has_value());

    auto canonical = std::string{"low"};
    CHECK(table->validate("crf", canonical) == std::nullopt);
    CHECK(canonical == "0");

    auto invalid = std::string{"99"};
    auto const error = table->validate("crf", invalid);
    REQUIRE(error.has_value());
    CHECK_FALSE(error->empty());
  }

  // The real `color` option carries its transform as a validator, so the
  // copied chain canonicalizes registered keys too.
  SECTION("the registered color entry") {
    auto value = std::string{"ALWAYS"};
    CHECK(realTable().validate("color", value) == std::nullopt);
    CHECK(value == "always");
  }
}
