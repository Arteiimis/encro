// User-level configuration store: file resolution, load (missing = empty,
// malformed = error), canonical pretty save, and the config-key table built at
// registration time from the options that back a config key (design D1/D3).
#pragma once

#include "core/error_handle.h"

#include <CLI/Validators.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace configstore {

enum class JsonKind {
  String,
  Number,  // integral: stored and validated as an integer
  Real,    // fractional: validated as a decimal number
  Boolean,
};

// One configurable key, captured while its CLI option is registered. The entry
// holds values, not a CLI::Option* (design D1), so it outlives the option tree.
struct KeyDef {
  std::string_view key;                    // token name (string literal)
  JsonKind kind;                           // derived from the binding type (D2)
  std::string longName;                    // "--crf"
  std::string builtinDefault;              // option->get_default_str() at registration
  std::vector<CLI::Validator> validators;  // copied in run order (transform first)

  // Schema metadata, also captured at registration (command-templates D5): the
  // option's help text, its enumerated legal values when the spec declares
  // them, and the numeric bound the spec's validators enforce.
  std::string desc;
  std::vector<std::string> enumValues;
  std::optional<double> minimum;
  std::optional<double> maximum;
  bool minimumExclusive = false;  // the bound itself is not allowed
};

struct KeyTable {
  std::vector<KeyDef> keys;  // canonical (file-write) order

  auto find(std::string_view key) const -> KeyDef const*;

  // Requires `key` present in the table: every caller rejects unknown keys
  // first. Applies the copied validators to `value` in place (transformers may
  // canonicalize it); returns the first validation error, or nullopt when
  // valid. Boolean/number keys get a type check after the validators: flag
  // options carry no validators, and the CLI would reject a non-integer at
  // conversion time.
  auto validate(std::string_view key, std::string& value) const
    -> std::optional<std::string>;
};

// Configurable keys in canonical (file-write) order, grouped like the CLI
// help groups. A file-format contract rather than a derivation: `model-dir`
// is registered inside the organize subcommand yet written last.
auto canonicalKeyOrder() -> std::span<std::string_view const>;

// Templateable keys in canonical (file-write) order, grouped like the CLI help
// groups: the main command's value options and flags, minus the options that
// carry input locations, the job-state file, and -v. A second contract list
// over the same registration capture (command-templates design D1).
auto canonicalTemplateKeyOrder() -> std::span<std::string_view const>;

// Orders registration-time `entries` by `order` and rejects a token whose key
// is not in the order, an order entry no token registers, and a duplicate.
// Pure: no app, no CLI::Option*, no globals. `tableName` labels the error, so
// the template table does not report itself as the config table.
auto assembleKeyTable(
  std::span<std::string_view const> order,
  std::span<KeyDef const> entries,
  std::string_view tableName = "config key table"
) -> eh::Result<KeyTable>;

// Resolution order: ENCRO_CONFIG; then the platform user-config root
// (LOCALAPPDATA/APPDATA fallback on Windows, XDG_CONFIG_HOME/~/.config
// fallback otherwise); temp dir as the terminal fallback.
auto resolveConfigPath() -> std::filesystem::path;

// NOLINTNEXTLINE(bugprone-exception-escape): standard-container members only, move is noexcept in practice
struct LoadResult {
  std::map<std::string, std::string> values;         // scalar values as canonical text
  std::map<std::string, std::string> unknownValues;  // raw JSON text of unknown keys
  std::vector<std::string> unknownKeys;              // reported once by the caller
  std::optional<std::string> error;                  // malformed JSON / bad value type
};

// `fileLabel` names the kind of file in the error text, so a template file
// reports "invalid template file ..." rather than "invalid config file ...".
auto load(
  std::filesystem::path const& path,
  KeyTable const& table,
  std::string_view fileLabel = "config file"
) -> LoadResult;

// Prints one warning line per unknown key found by load().
void warnUnknownKeys(LoadResult const& loaded, std::filesystem::path const& path);

// JSON text for one stored scalar on a single line; numbers/booleans stay
// unquoted so the file keeps native JSON types.
auto scalarJsonText(std::string const& raw, JsonKind kind) -> std::string;

// Rewrites the whole file in canonical pretty form (creates parent dirs).
auto save(
  std::filesystem::path const& path,
  std::map<std::string, std::string> const& values,
  KeyTable const& table
) -> std::optional<std::string>;

}  // namespace configstore
