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
  Number,
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

// Orders registration-time `entries` by `order` and rejects a token whose key
// is not in the order, an order entry no token registers, and a duplicate.
// Pure: no app, no CLI::Option*, no globals.
auto assembleKeyTable(
  std::span<std::string_view const> order,
  std::span<KeyDef const> entries
) -> eh::Result<KeyTable>;

// Resolution order: ENCRO_CONFIG; then the platform user-config root
// (LOCALAPPDATA/APPDATA fallback on Windows, XDG_CONFIG_HOME/~/.config
// fallback otherwise); temp dir as the terminal fallback.
auto resolveConfigPath() -> std::filesystem::path;

struct
  LoadResult {  // NOLINT(bugprone-exception-escape): standard-container members only, move is noexcept in practice
  std::map<std::string, std::string> values;  // scalar values as canonical text
  std::vector<std::string> unknownKeys;       // reported once by the caller
  std::optional<std::string> error;           // malformed JSON / bad value type
};

auto load(std::filesystem::path const& path, KeyTable const& table) -> LoadResult;

// Prints one warning line per unknown key found by load().
void warnUnknownKeys(LoadResult const& loaded, std::filesystem::path const& path);

// Rewrites the whole file in canonical pretty form (creates parent dirs).
auto save(
  std::filesystem::path const& path,
  std::map<std::string, std::string> const& values,
  KeyTable const& table
) -> std::optional<std::string>;

}  // namespace configstore
