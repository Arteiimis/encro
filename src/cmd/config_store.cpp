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

// ── Key table (design D1/D3) ────────────────────────────────────────────────
// Canonical order mirrors the CLI help groups (General / IO / Processing /
// FileOp); save() writes keys in this order so the file is diff-stable. The
// order is a contract, not a derivation of registration order: `model-dir` is
// registered inside the organize subcommand yet written last.

inline constexpr auto kCanonicalKeyOrder = std::to_array<std::string_view>({
  "color",
  "yes",
  "output-format",
  "keep",
  "force-conflict-handling",
  "folder-summary",
  "recursive",
  "jobs",
  "ffmpeg-path",
  "compress",
  "image-quality",
  "crf",
  "min-vmaf",
  "preset",
  "video-codec",
  "pack",
  "model-dir",
});

auto canonicalKeyOrder() -> std::span<std::string_view const> {
  return kCanonicalKeyOrder;
}

auto KeyTable::find(std::string_view key) const -> KeyDef const* {
  auto const it =
    std::ranges::find_if(keys, [key](KeyDef const& def) { return def.key == key; });
  return it == keys.end() ? nullptr : &*it;
}

auto assembleKeyTable(
  std::span<std::string_view const> order,
  std::span<KeyDef const> entries
) -> eh::Result<KeyTable> {
  auto table = KeyTable{};
  table.keys.reserve(order.size());

  for (auto const& key: order) {
    auto const* entry = static_cast<KeyDef const*>(nullptr);
    auto matches = 0;
    for (auto const& candidate: entries) {
      if (candidate.key != key) { continue; }
      ++matches;
      entry = &candidate;
    }
    if (matches == 0) {
      return eh::makeError("config key table: no option registers key '{}'", key);
    }
    if (matches > 1) {
      return eh::makeError(
        "config key table: key '{}' is registered more than once",
        key
      );
    }
    table.keys.push_back(*entry);
  }

  for (auto const& entry: entries) {
    if (std::ranges::find(order, entry.key) == order.end()) {
      return eh::makeError(
        "config key table: key '{}' is missing from the canonical order",
        entry.key
      );
    }
  }
  return table;
}

// Applies the copied validators to `value` in place (transformers may
// canonicalize it); returns the first validation error, or nullopt when valid.
// After the option's own validators, boolean/number keys get a type check on
// the final value: flag options carry no validators, and the CLI would reject
// a non-integer at conversion time. Requires a key present in the table
// (design D7): every caller rejects unknown keys first.
auto KeyTable::validate(std::string_view key, std::string& value) const
  -> std::optional<std::string> {
  auto const* def = find(key);

  for (auto const& validator: def->validators) {
    if (auto error = validator(value); !error.empty()) { return error; }
  }

  if (def->kind == JsonKind::Boolean) {
    if (value != "true" && value != "false") {
      return std::format("expected true or false, got '{}'", value);
    }
  } else if (def->kind == JsonKind::Number) {
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

auto load(fs::path const& path, KeyTable const& table) -> LoadResult {
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
    if (table.find(entry.key()) == nullptr) {
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

auto save(
  fs::path const& path,
  std::map<std::string, std::string> const& values,
  KeyTable const& table
) -> std::optional<std::string> {
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
  for (auto const& def: table.keys) {
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
