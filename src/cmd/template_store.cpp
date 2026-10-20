#include "cmd/template_store.h"
#include "core/path_utf8.h"

#include <boost/json.hpp>  // IWYU pragma: keep

#include <algorithm>
#include <cctype>
#include <format>
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

bool isNameChar(char ch) {
  return (ch >= 'a' && ch <= 'z')
    || (ch >= '0' && ch <= '9')
    || ch == '.'
    || ch == '_'
    || ch == '-';
}

// Letters and digits only, lowercase: the first character's rule.
bool isLowerAlnum(char ch) {
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

// The built-in default as JSON. A flag declares no default string, and every
// templateable flag defaults to false, so a boolean always reports one.
auto defaultJson(configstore::KeyDef const& def) -> std::optional<std::string> {
  switch (def.kind) {
    case configstore::JsonKind::Boolean:
      return def.builtinDefault == "true" ? "true" : "false";
    case configstore::JsonKind::Number:
    case configstore::JsonKind::Real:
      if (def.builtinDefault.empty()) { return std::nullopt; }
      return def.builtinDefault;  // registration-time literal, already JSON
    case configstore::JsonKind::String:
      if (def.builtinDefault.empty()) { return std::nullopt; }
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

auto resolveTemplatesDir() -> fs::path {
  return configstore::resolveConfigPath().parent_path() / "templates";
}

auto normalizeName(std::string_view name) -> std::string {
  auto const normalized = lowercased(name);
  if (normalized.empty() || normalized == reservedStem()) { return {}; }
  if (!isLowerAlnum(normalized.front())) { return {}; }
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

auto load(fs::path const& path, configstore::KeyTable const& table)
  -> configstore::LoadResult {
  auto loaded = configstore::load(path, table, "template file");
  // The schema reference is part of the format, not a stored option: it is
  // neither applied nor warned about, and a rewrite writes its own.
  std::erase(loaded.unknownKeys, std::string{"$schema"});
  loaded.unknownValues.erase("$schema");
  return loaded;
}

auto save(
  fs::path const& path,
  std::map<std::string, std::string> const& values,
  std::map<std::string, std::string> const& unknownValues,
  configstore::KeyTable const& table
) -> std::optional<std::string> {
  // Hand-pretty-printed like the config file: one key per line, known keys in
  // canonical order, `$schema` first, unknown keys after (preserved, not lost).
  auto body = std::format("{{\n    \"$schema\": \"{}\"", kSchemaRef);
  for (auto const& def: table.keys) {
    auto const it = values.find(std::string{def.key});
    if (it == values.end()) { continue; }
    body += ",\n    ";
    body += json::serialize(json::string(it->first));
    body += ": ";
    body += configstore::scalarJsonText(it->second, def.kind);
  }
  for (auto const& [key, raw]: unknownValues) {
    body += ",\n    ";
    body += json::serialize(json::string(key));
    body += ": ";
    body += raw;
  }
  body += "\n}\n";
  return configstore::writeTextFile(path, body, "template file");
}

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
  return configstore::writeTextFile(
    dir / std::string{kSchemaFileName},
    schema(table),
    "template schema"
  );
}

auto unknownTemplateMessage(std::string_view name) -> std::string {
  return std::format("unknown template: {} (run 'encro template list')", name);
}

}  // namespace templatestore
