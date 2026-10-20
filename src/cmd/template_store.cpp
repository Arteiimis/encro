#include "cmd/template_store.h"
#include "core/path_utf8.h"

#include <boost/json.hpp>  // IWYU pragma: keep

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace templatestore {

namespace json = boost::json;

namespace {

// The schema file's name without the `.json` extension: the stem a template
// must not take.
auto reservedStem() -> std::string_view {
  auto const name = kSchemaFileName;
  return name.substr(0, name.size() - std::string_view{".json"}.size());
}

auto lowercased(std::string_view text) -> std::string {
  auto out = std::string{text};
  std::ranges::transform(out, out.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return out;
}

auto isNameChar(char ch) -> bool {
  return (ch >= 'a' && ch <= 'z')
    || (ch >= '0' && ch <= '9')
    || ch == '.'
    || ch == '_'
    || ch == '-';
}

auto isAlnum(char ch) -> bool {
  return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}

// Template `.json` files in `dir`, by lowercase stem, schema file excluded.
auto stemsIn(fs::path const& dir) -> std::vector<std::string> {
  auto stems = std::vector<std::string>{};
  auto ec = std::error_code{};
  if (!fs::is_directory(dir, ec) || ec) { return stems; }

  for (auto const& entry: fs::directory_iterator(dir, ec)) {
    if (ec) { break; }
    if (!entry.is_regular_file(ec) || ec) { continue; }
    auto const& path = entry.path();
    if (path.extension() != ".json") { continue; }
    auto stem = lowercased(path.stem().string());
    if (stem == reservedStem()) { continue; }
    stems.push_back(std::move(stem));
  }
  return stems;
}

}  // namespace

auto resolveTemplatesDir() -> fs::path {
  return configstore::resolveConfigPath().parent_path() / "templates";
}

auto normalizeName(std::string_view name) -> std::string {
  auto const normalized = lowercased(name);
  if (normalized.empty() || normalized == reservedStem()) { return {}; }
  if (!isAlnum(normalized.front())) { return {}; }
  if (!std::ranges::all_of(normalized, isNameChar)) { return {}; }
  return normalized;
}

auto find(fs::path const& dir, std::string_view name) -> std::optional<fs::path> {
  auto const normalized = normalizeName(name);
  if (normalized.empty()) { return std::nullopt; }

  auto const stems = stemsIn(dir);
  if (std::ranges::find(stems, normalized) == stems.end()) { return std::nullopt; }
  return dir / (normalized + ".json");
}

auto list(fs::path const& dir) -> std::vector<std::string> {
  auto stems = stemsIn(dir);
  std::ranges::sort(stems);
  stems.erase(std::unique(stems.begin(), stems.end()), stems.end());
  return stems;
}

auto pathFor(fs::path const& dir, std::string_view name) -> std::optional<fs::path> {
  auto const normalized = normalizeName(name);
  if (normalized.empty()) { return std::nullopt; }
  return dir / (normalized + ".json");
}

auto load(fs::path const& path, configstore::KeyTable const& table) -> LoadResult {
  auto result = LoadResult{};

  auto loaded = configstore::load(path, table, "template file");
  result.error = std::move(loaded.error);
  result.values = std::move(loaded.values);
  for (auto const& key: loaded.unknownKeys) {
    // The schema reference is part of the format, not a stored option.
    if (key == "$schema") { continue; }
    result.unknownKeys.push_back(key);
  }
  for (auto const& [key, raw]: loaded.unknownValues) {
    if (key == "$schema") { continue; }
    result.unknownValues.emplace(key, raw);
  }
  return result;
}

auto save(
  fs::path const& path,
  std::map<std::string, std::string> const& values,
  std::map<std::string, std::string> const& unknownValues,
  configstore::KeyTable const& table
) -> std::optional<std::string> {
  auto ec = std::error_code{};
  if (path.has_parent_path()) {
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
      return std::format(
        "cannot create template directory {}: {}",
        pathutf8::fromPath(path.parent_path()),
        ec.message()
      );
    }
  }

  auto out = std::ofstream{path, std::ios::binary};
  if (!out.is_open()) {
    return std::format("cannot write template file: {}", pathutf8::fromPath(path));
  }

  // Hand-pretty-printed like the config file: one key per line, known keys in
  // canonical order, `$schema` first, unknown keys after (preserved, not lost).
  out << "{\n    \"$schema\": \"" << kSchemaRef << "\"";
  for (auto const& def: table.keys) {
    auto const it = values.find(std::string{def.key});
    if (it == values.end()) { continue; }
    out
      << ",\n    "
      << json::serialize(json::string(it->first))
      << ": "
      << configstore::scalarJsonText(it->second, def.kind);
  }
  for (auto const& [key, raw]: unknownValues) {
    out << ",\n    " << json::serialize(json::string(key)) << ": " << raw;
  }
  out << "\n}\n";
  out.flush();
  if (!out) {
    return std::format("failed to write template file: {}", pathutf8::fromPath(path));
  }
  return std::nullopt;
}

namespace {

// JSON Schema type name for a key's stored kind.
auto schemaType(configstore::JsonKind kind) -> std::string_view {
  switch (kind) {
    case configstore::JsonKind::String : return "string";
    case configstore::JsonKind::Number : return "integer";
    case configstore::JsonKind::Real   : return "number";
    case configstore::JsonKind::Boolean: return "boolean";
  }
  return "string";
}

// The built-in default as JSON, or nothing when the flag declares none.
auto defaultJson(configstore::KeyDef const& def) -> std::optional<std::string> {
  if (def.builtinDefault.empty()) { return std::nullopt; }
  switch (def.kind) {
    case configstore::JsonKind::Boolean:
      return def.builtinDefault == "true" ? "true" : "false";
    case configstore::JsonKind::Number: {
      auto parsed = 0LL;
      auto const [ptr, ec] = std::from_chars(
        def.builtinDefault.data(),
        def.builtinDefault.data() + def.builtinDefault.size(),
        parsed
      );
      if (
        ec != std::errc{} || ptr != def.builtinDefault.data() + def.builtinDefault.size()
      ) {
        return std::nullopt;
      }
      return std::format("{}", parsed);
    }
    case configstore::JsonKind::Real: {
      auto parsed = 0.0;
      auto const [ptr, ec] = std::from_chars(
        def.builtinDefault.data(),
        def.builtinDefault.data() + def.builtinDefault.size(),
        parsed
      );
      if (
        ec != std::errc{} || ptr != def.builtinDefault.data() + def.builtinDefault.size()
      ) {
        return std::nullopt;
      }
      return std::format("{}", parsed);
    }
    case configstore::JsonKind::String:
      return json::serialize(json::string{def.builtinDefault});
  }
  return std::nullopt;
}

// One property schema, on a single line: type, legal values or bounds, the
// built-in default, and the option's help text.
auto propertySchema(configstore::KeyDef const& def) -> std::string {
  auto out = std::format("{{\"type\": \"{}\"", schemaType(def.kind));
  if (!def.enumValues.empty()) {
    out += ", \"enum\": [";
    auto first = true;
    for (auto const& legal: def.enumValues) {
      if (!first) { out += ", "; }
      first = false;
      out += json::serialize(json::string{legal});
    }
    out += "]";
  }
  if (def.minimum.has_value()) {
    out += def.minimumExclusive ? ", \"exclusiveMinimum\": " : ", \"minimum\": ";
    out += std::format("{}", *def.minimum);
  }
  if (def.maximum.has_value()) { out += std::format(", \"maximum\": {}", *def.maximum); }
  if (auto const fallback = defaultJson(def); fallback.has_value()) {
    out += std::format(", \"default\": {}", *fallback);
  }
  if (!def.desc.empty()) {
    out += ", \"description\": ";
    out += json::serialize(json::string{def.desc});
  }
  out += "}";
  return out;
}

}  // namespace

auto schema(configstore::KeyTable const& table) -> std::string {
  auto out = std::string{};
  out += "{\n";
  out += "  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n";
  out += "  \"title\": \"encro command template\",\n";
  out += "  \"type\": \"object\",\n";
  out += "  \"additionalProperties\": false,\n";
  out += "  \"properties\": {\n";
  out += "    \"$schema\": {\"type\": \"string\"}";
  for (auto const& def: table.keys) {
    out += ",\n    ";
    out += json::serialize(json::string{std::string{def.key}});
    out += ": ";
    out += propertySchema(def);
  }
  out += "\n  }\n}\n";
  return out;
}

auto writeSchema(fs::path const& dir, configstore::KeyTable const& table)
  -> std::optional<std::string> {
  auto ec = std::error_code{};
  fs::create_directories(dir, ec);
  if (ec) {
    return std::format(
      "cannot create template directory {}: {}",
      pathutf8::fromPath(dir),
      ec.message()
    );
  }

  auto const path = dir / std::string{kSchemaFileName};
  auto out = std::ofstream{path, std::ios::binary};
  if (!out.is_open()) {
    return std::format("cannot write template schema: {}", pathutf8::fromPath(path));
  }
  out << schema(table);
  out.flush();
  if (!out) {
    return std::format("failed to write template schema: {}", pathutf8::fromPath(path));
  }
  return std::nullopt;
}

}  // namespace templatestore
