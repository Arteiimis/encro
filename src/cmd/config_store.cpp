#include "cmd/config_store.h"

#include "infra/env.h"
#include "infra/terminal.h"

#include <boost/json.hpp>  // IWYU pragma: keep

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>  // IWYU pragma: keep -- needed with MSVC STL; Linux libstdc++ pulls it transitively

namespace fs = std::filesystem;

using enum terminal::MessageKind;

namespace configstore {

// ── Key table ───────────────────────────────────────────────────────────────
// Canonical order mirrors the CLI help groups (General / IO / Processing /
// FileOp); save() writes keys in this order so the file is diff-stable.

inline constexpr auto kKeys = std::array<KeyDef, 16>{{
  {"color", JsonKind::String},
  {"yes", JsonKind::Boolean},
  {"output-format", JsonKind::String},
  {"keep", JsonKind::Boolean},
  {"force-conflict-handling", JsonKind::String},
  {"folder-summary", JsonKind::Boolean},
  {"recursive", JsonKind::Boolean},
  {"jobs", JsonKind::Number},
  {"ffmpeg-path", JsonKind::String},
  {"compress", JsonKind::Boolean},
  {"image-quality", JsonKind::Number},
  {"crf", JsonKind::Number},
  {"min-vmaf", JsonKind::Number},
  {"preset", JsonKind::String},
  {"video-codec", JsonKind::String},
  {"pack", JsonKind::Boolean},
}};

auto keys() -> std::span<KeyDef const> {
  return kKeys;
}

bool isKnownKey(std::string_view key) {
  return std::ranges::any_of(kKeys, [key](KeyDef const& def) { return def.key == key; });
}

auto jsonKindOf(std::string_view key) -> std::optional<JsonKind> {
  auto const it =
    std::ranges::find_if(kKeys, [key](KeyDef const& def) { return def.key == key; });
  return it == kKeys.end() ? std::nullopt : std::optional{it->kind};
}

// ── Validator registry (design D3) ─────────────────────────────────────────
// Filled during CLI registration (cfg::ConfigKey tokens). The option pointers
// are owned by the CLI app allocated in commandLineInit, which intentionally
// outlives the call (leaked, process lifetime) so config-command actions can
// still reach the validators and built-in defaults after parsing.

auto configKeyRegistry() -> std::map<std::string, CLI::Option*>& {
  static auto registry = std::map<std::string, CLI::Option*>{};
  return registry;
}

void captureConfigKey(std::string_view key, CLI::Option* option) {
  configKeyRegistry().insert_or_assign(std::string{key}, option);
}

// Applies the registered validators to `value` in place (transformers may
// canonicalize it); returns the first validation error, or empty when valid.
// After the option's own validators, boolean/number keys get a type check on
// the final value: flag options carry no validators, and the CLI would reject
// a non-integer at conversion time.
auto validateValue(std::string_view key, std::string& value)
  -> std::optional<std::string> {
  auto const kind = jsonKindOf(key);
  auto const& registry = configKeyRegistry();
  auto const it = registry.find(std::string{key});
  if (it == registry.end()) { return std::nullopt; }

  for (auto index = 0;; ++index) {
    auto* validator = [&]() -> CLI::Validator* {
      try {
        return it->second->get_validator(index);
      } catch (CLI::OptionNotFound const&) { return nullptr; }
    }();
    if (validator == nullptr) { break; }

    if (auto error = (*validator)(value); !error.empty()) { return error; }
  }

  if (kind == JsonKind::Boolean) {
    if (value != "true" && value != "false") {
      return std::format("expected true or false, got '{}'", value);
    }
  } else if (kind == JsonKind::Number) {
    long long parsed = 0;
    auto const [ptr, ec] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc{} || ptr != value.data() + value.size()) {
      return std::format("expected an integer, got '{}'", value);
    }
  }
  return std::nullopt;
}

// ── Path resolution ─────────────────────────────────────────────────────────

auto resolveConfigPath() -> fs::path {
  if (auto const overridePath = processenv::readNonEmptyEnvVar("ENCRO_CONFIG")) {
    return fs::path{*overridePath};
  }
#if defined(_WIN32)
  if (auto const local = processenv::readNonEmptyEnvVar("LOCALAPPDATA")) {
    return fs::path{*local} / "encro" / "config.json";
  }
  if (auto const roaming = processenv::readNonEmptyEnvVar("APPDATA")) {
    return fs::path{*roaming} / "encro" / "config.json";
  }
#else
  if (auto const xdg = processenv::readNonEmptyEnvVar("XDG_CONFIG_HOME")) {
    return fs::path{*xdg} / "encro" / "config.json";
  }
  if (auto const home = processenv::readNonEmptyEnvVar("HOME")) {
    return fs::path{*home} / ".config" / "encro" / "config.json";
  }
#endif
  return fs::temp_directory_path() / "encro" / "config.json";
}

// ── Load / save ─────────────────────────────────────────────────────────────

namespace {

namespace json = boost::json;

auto stringifyScalar(json::value const& value) -> std::optional<std::string> {
  if (value.is_string()) { return std::string{value.as_string().c_str()}; }
  if (value.is_bool() || value.is_int64() || value.is_uint64() || value.is_double()) {
    return json::serialize(value);
  }
  return std::nullopt;
}

// JSON text for one stored scalar; numbers/booleans stay unquoted so the file
// keeps native JSON types. A number-kind value that is not numeric falls back
// to a quoted string (defensive; set-time validation keeps this from happening).
auto scalarJson(std::string const& raw, JsonKind kind) -> std::string {
  switch (kind) {
    case JsonKind::Boolean: return raw == "true" ? "true" : "false";
    case JsonKind::Number : {
      auto value = double{0};
      auto const* end = raw.data();
      auto const [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), value);
      if (ec == std::errc{} && ptr == raw.data() + raw.size()) { return raw; }
      return json::serialize(json::string(raw));
    }
    case JsonKind::String: break;
  }
  return json::serialize(json::string(raw));
}

}  // namespace

void warnUnknownKeys(LoadResult const& loaded, fs::path const& path) {
  for (auto const& key: loaded.unknownKeys) {
    terminal::eprintln(
      Warning,
      "ignoring unknown config key \"{}\" in {}",
      key,
      path.string()
    );
  }
}

auto load(fs::path const& path) -> LoadResult {
  auto result = LoadResult{};

  auto ec = std::error_code{};
  if (!fs::exists(path, ec) || ec) { return result; }  // missing file = no config

  auto file = std::ifstream{path, std::ios::binary};
  if (!file.is_open()) {
    result.error = std::format("cannot open config file: {}", path.string());
    return result;
  }

  auto const content = std::string{std::istreambuf_iterator<char>{file}, {}};
  auto jsonEc = boost::system::error_code{};
  auto const parsed = json::parse(content, jsonEc);
  if (jsonEc) {
    result.error =
      std::format("invalid config file {}: {}", path.string(), jsonEc.message());
    return result;
  }
  if (!parsed.is_object()) {
    result.error = std::format(
      "invalid config file {}: top-level value must be a JSON object",
      path.string()
    );
    return result;
  }

  for (auto const& entry: parsed.as_object()) {
    if (!isKnownKey(entry.key())) {
      result.unknownKeys.emplace_back(entry.key());
      continue;
    }
    if (auto const text = stringifyScalar(entry.value()); text.has_value()) {
      result.values.insert_or_assign(std::string{entry.key()}, *text);
    } else {
      result.error = std::format(
        "invalid config file {}: key \"{}\" must be a scalar value",
        path.string(),
        entry.key()
      );
      return result;
    }
  }
  return result;
}

auto save(fs::path const& path, std::map<std::string, std::string> const& values)
  -> std::optional<std::string> {
  auto ec = std::error_code{};
  if (path.has_parent_path()) {
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
      return std::format(
        "cannot create config directory {}: {}",
        path.parent_path().string(),
        ec.message()
      );
    }
  }

  auto out = std::ofstream{path, std::ios::binary};
  if (!out.is_open()) {
    return std::format("cannot write config file: {}", path.string());
  }

  // Flat object, pretty-printed by hand: one key per line in canonical table
  // order. Deterministic output keeps the file hand-editable and diff-stable.
  out << "{\n";
  auto first = true;
  for (auto const& def: kKeys) {
    auto const it = values.find(std::string{def.key});
    if (it == values.end()) { continue; }
    if (!first) { out << ",\n"; }
    first = false;
    out
      << "    "
      << json::serialize(json::string(it->first))
      << ": "
      << scalarJson(it->second, def.kind);
  }
  out << (first ? "}\n" : "\n}\n");
  out.flush();
  if (!out) { return std::format("failed to write config file: {}", path.string()); }
  return std::nullopt;
}

}  // namespace configstore
