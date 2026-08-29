// User-level configuration store: file resolution, load (missing = empty,
// malformed = error), canonical pretty save, and the config-key registry that
// ties config keys to their CLI options (design D2/D3).
#pragma once

#include <CLI/Option.hpp>
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

struct KeyDef {
  std::string_view key;
  JsonKind kind;
};

// Configurable keys in canonical (file-write) order, grouped like the CLI
// help groups.
auto keys() -> std::span<KeyDef const>;

auto isKnownKey(std::string_view key) -> bool;
auto jsonKindOf(std::string_view key) -> std::optional<JsonKind>;

// Registry tying config keys to their registered CLI options. Pointers are
// owned by the CLI app in commandLineInit, which intentionally outlives the
// call, so config-command actions can read validators and built-in defaults
// after parsing.
auto configKeyRegistry() -> std::map<std::string, CLI::Option*>&;
void captureConfigKey(std::string_view key, CLI::Option* option);

// Applies the registered validators to `value` in place (transformers may
// canonicalize it); returns the first validation error, or nullopt when valid
// (or when the key has no registered option).
auto validateValue(std::string_view key, std::string& value)
  -> std::optional<std::string>;

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

auto load(std::filesystem::path const& path) -> LoadResult;

// Rewrites the whole file in canonical pretty form (creates parent dirs).
auto save(
  std::filesystem::path const& path,
  std::map<std::string, std::string> const& values
) -> std::optional<std::string>;

}  // namespace configstore
