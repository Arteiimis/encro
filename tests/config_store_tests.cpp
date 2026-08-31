#include "cmd/config_store.h"
#include "test_utils.h"

#include <boost/json.hpp>        // IWYU pragma: keep

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

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

auto findKey(std::string_view key) -> configstore::KeyDef const& {
  for (auto const& def: configstore::keys()) {
    if (def.key == key) { return def; }
  }
  throw std::logic_error("unknown test key");
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

  auto const loaded = configstore::load(configstore::resolveConfigPath());
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

  auto const loaded = configstore::load(path);
  REQUIRE(loaded.error.has_value());
  CHECK(loaded.error->find(path.string()) != std::string::npos);
}

TEST_CASE("loading a non-object config file fails", "[cmd][config-store]") {
  auto const temp = TempDir{};
  auto const path = temp.path / "array.json";
  testutils::writeTextFile(path, "[1, 2, 3]");
  auto const guard = ConfigEnvScope(path);

  auto const loaded = configstore::load(path);
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

  auto const loaded = configstore::load(path);
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

  auto const loaded = configstore::load(path);
  REQUIRE(loaded.error.has_value());
  CHECK(loaded.error->find("jobs") != std::string::npos);
}

TEST_CASE("key table knows the configurable keys", "[cmd][config-store]") {
  CHECK(configstore::keys().size() == 16);
  CHECK(configstore::isKnownKey("crf"));
  CHECK(configstore::isKnownKey("folder-summary"));
  CHECK_FALSE(configstore::isKnownKey("dry-run"));
  CHECK_FALSE(configstore::isKnownKey("output"));
  CHECK(configstore::jsonKindOf("jobs") == configstore::JsonKind::Number);
  CHECK(configstore::jsonKindOf("yes") == configstore::JsonKind::Boolean);
  CHECK(configstore::jsonKindOf("preset") == configstore::JsonKind::String);
}

TEST_CASE("save writes pretty canonical form and round-trips", "[cmd][config-store]") {
  auto const temp = TempDir{};
  auto const path = temp.path / "nested" / "config.json";

  auto values = std::map<std::string, std::string>{};
  values["crf"] = "23";
  values["jobs"] = "4";
  values["pack"] = "true";
  values["preset"] = "p5";

  REQUIRE_FALSE(configstore::save(path, values).has_value());

  auto const text = testutils::readTextFile(path);
  CHECK(text.starts_with("{\n"));
  CHECK(text.find("\n    ") != std::string::npos);          // indented keys
  CHECK(text.find("\"crf\": 23") != std::string::npos);     // native number
  CHECK(text.find("\"pack\": true") != std::string::npos);  // native boolean
  CHECK(text.find("\"jobs\"") < text.find("\"crf\""));      // canonical order

  auto const guard = ConfigEnvScope(path);
  auto const loaded = configstore::load(path);
  REQUIRE_FALSE(loaded.error.has_value());
  CHECK(loaded.values == values);
}

TEST_CASE("save escapes string values", "[cmd][config-store]") {
  auto const temp = TempDir{};
  auto const path = temp.path / "config.json";

  auto values = std::map<std::string, std::string>{};
  values["ffmpeg-path"] = "D:\\tools\\ffmpeg";

  REQUIRE_FALSE(configstore::save(path, values).has_value());

  auto const text = testutils::readTextFile(path);
  CHECK(text.find("\"ffmpeg-path\": \"D:\\\\tools\\\\ffmpeg\"") != std::string::npos);
}

TEST_CASE("save of an empty store writes an empty JSON object", "[cmd][config-store]") {
  auto const temp = TempDir{};
  auto const path = temp.path / "config.json";

  REQUIRE_FALSE(configstore::save(path, {}).has_value());
  CHECK(testutils::readTextFile(path) == "{\n}\n");
}

TEST_CASE("validateValue applies registered validators in order", "[cmd][config-store]") {
  auto app = CLI::App{};
  auto flag = false;
  auto value = 0;
  auto* option = app.add_option("--crf", value, "test");
  option->transform(
    CLI::CheckedTransformer{
      std::vector<std::pair<std::string, std::string>>{{"low", "0"}, {"high", "51"}},
    }
  );
  option->check(CLI::Range(0, 51));
  configstore::captureConfigKey("crf", option);

  auto canonical = std::string{"low"};
  CHECK(configstore::validateValue("crf", canonical) == std::nullopt);
  CHECK(canonical == "0");

  auto invalid = std::string{"99"};
  auto const error = configstore::validateValue("crf", invalid);
  REQUIRE(error.has_value());
  CHECK_FALSE(error->empty());

  auto unregistered = std::string{"anything"};
  CHECK(configstore::validateValue("no-such-key", unregistered) == std::nullopt);
}
