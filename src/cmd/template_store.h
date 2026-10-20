// Command-template store (spec: command-templates): one JSON file per template
// under the templates directory beside the config file, the name rules, and a
// writer that preserves keys outside the templateable set. A template file is a
// config file in another directory, so loading reuses the config store's table
// and validation.
#pragma once

#include "cmd/config_store.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace templatestore {

// The generated schema's file name, reserved inside the templates directory: it
// is never listed as a template and never selectable by name.
inline constexpr std::string_view kSchemaFileName = "templates.schema.json";
// What each template file's `$schema` key points at, resolved by the editor
// against the file's own location.
inline constexpr std::string_view kSchemaRef = "./templates.schema.json";

// `<config file's directory>/templates`, so ENCRO_CONFIG relocates templates
// together with the config file.
auto resolveTemplatesDir() -> std::filesystem::path;

// Lowercase, generated from the template name. Empty when the name is illegal:
// empty, not starting with a letter or digit, containing anything outside
// `[a-z0-9._-]`, or reserved for the schema file.
auto normalizeName(std::string_view name) -> std::string;

// Case-insensitive lookup of an existing template file; nullopt when absent, so
// an unknown name is reported by the caller instead of a file-system error.
auto find(std::filesystem::path const& dir, std::string_view name)
  -> std::optional<std::filesystem::path>;

// Existing template names, lowercase and sorted; the schema file excluded. A
// missing directory is an empty set.
auto list(std::filesystem::path const& dir) -> std::vector<std::string>;

// Path a template with `name` lives at, whether or not the file exists. The
// name is normalized first; empty when it is illegal.
auto pathFor(std::filesystem::path const& dir, std::string_view name)
  -> std::optional<std::filesystem::path>;

// NOLINTNEXTLINE(bugprone-exception-escape): standard-container members only
struct LoadResult {
  std::map<std::string, std::string> values;         // canonical scalar text
  std::map<std::string, std::string> unknownValues;  // raw JSON, preserved on write
  std::vector<std::string> unknownKeys;              // warned about once
  std::optional<std::string> error;                  // malformed JSON
};

// Reads one template file. `$schema` is reserved: it never reaches values,
// unknownKeys or unknownValues.
auto load(std::filesystem::path const& path, configstore::KeyTable const& table)
  -> LoadResult;

// Rewrites the file canonically: `$schema` first, then the known keys in table
// order, then whatever unknown keys the file carried.
auto save(
  std::filesystem::path const& path,
  std::map<std::string, std::string> const& values,
  std::map<std::string, std::string> const& unknownValues,
  configstore::KeyTable const& table
) -> std::optional<std::string>;

// JSON Schema for template files, rendered from the same key table that defines
// the templateable set (design D5), so the two cannot drift. `$schema` is
// declared as a string property because every template file carries it.
auto schema(configstore::KeyTable const& table) -> std::string;

// Writes the schema as kSchemaFileName inside `dir`, creating the directory.
auto writeSchema(std::filesystem::path const& dir, configstore::KeyTable const& table)
  -> std::optional<std::string>;

}  // namespace templatestore
