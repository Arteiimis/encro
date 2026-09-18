#include "cmd/config_command.h"

#include "cmd/config_store.h"
#include "infra/terminal.h"

#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace cmd {

using enum terminal::MessageKind;

namespace {

// Built-in default of a key, from the table entry captured at registration.
// Config actions run on the probe parse (no injection), so this is the
// built-in default, not a config value.
auto builtinDefault(configstore::KeyTable const& table, std::string_view key)
  -> std::string {
  auto const* def = table.find(key);
  return def == nullptr ? std::string{} : def->builtinDefault;
}

// Effective value and source for a key: config file first, built-in default
// otherwise.
auto effectiveValue(
  configstore::LoadResult const& loaded,
  configstore::KeyTable const& table,
  std::string_view key
) -> std::pair<std::string, bool> {
  auto const it = loaded.values.find(std::string{key});
  if (it != loaded.values.end()) { return {it->second, true}; }
  return {builtinDefault(table, key), false};
}

int reportUnknownKey(std::string_view key) {
  terminal::eprintln(
    Error,
    "unknown config key: {} (run 'encro config list' for the configurable keys)",
    key
  );
  return 1;
}

int reportSaveError(std::optional<std::string> const& error) {
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): all callers guard with has_value()
  terminal::eprintln(Error, "{}", *error);
  return 1;
}

int listAction(
  configstore::LoadResult const& loaded,
  configstore::KeyTable const& table
) {
  for (auto const& def: table.keys) {
    auto const [value, fromConfig] = effectiveValue(loaded, table, def.key);
    terminal::println(
      Plain,
      "{:<24} {:<14} ({})",
      def.key,
      value.empty() ? std::string{"\"\""} : value,
      fromConfig ? "config" : "default"
    );
  }
  return 0;
}

int getAction(
  configstore::LoadResult const& loaded,
  configstore::KeyTable const& table,
  std::string const& key
) {
  if (table.find(key) == nullptr) { return reportUnknownKey(key); }
  terminal::println(Plain, "{}", effectiveValue(loaded, table, key).first);
  return 0;
}

int setAction(
  configstore::LoadResult& loaded,
  std::filesystem::path const& configPath,
  configstore::KeyTable const& table,
  std::string const& key,
  std::string const& value
) {
  if (table.find(key) == nullptr) { return reportUnknownKey(key); }

  // validate canonicalizes transformed values in place.
  auto stored = value;
  if (auto const error = table.validate(key, stored); error.has_value()) {
    terminal::eprintln(Error, "invalid value for {}: {} ({})", key, value, *error);
    return 1;
  }

  loaded.values.insert_or_assign(key, stored);
  if (
    auto const saveError = configstore::save(configPath, loaded.values, table);
    saveError.has_value()
  ) {
    return reportSaveError(saveError);
  }
  return 0;
}

int unsetAction(
  configstore::LoadResult& loaded,
  std::filesystem::path const& configPath,
  configstore::KeyTable const& table,
  std::string const& key
) {
  if (table.find(key) == nullptr) { return reportUnknownKey(key); }
  if (loaded.values.erase(key) == 0) { return 0; }
  if (
    auto const saveError = configstore::save(configPath, loaded.values, table);
    saveError.has_value()
  ) {
    return reportSaveError(saveError);
  }
  return 0;
}

}  // namespace

int runConfigCommand(CmdParseResult const& cmd) {
  auto const& table = cmd.keyTable;
  auto const configPath = configstore::resolveConfigPath();

  // `path` only resolves the location; it never reads the file content.
  if (cmd.configVerb == "path") {
    terminal::println(Plain, "{}", terminal::path(configPath));
    return 0;
  }

  auto loaded = configstore::load(configPath, table);
  if (loaded.error) {
    terminal::eprintln(Error, "{}", *loaded.error);
    return 1;
  }
  warnUnknownKeys(loaded, configPath);

  // The arity validation in cmd.cpp guarantees the key/value positionals are
  // present for the verbs that need them.
  if (cmd.configVerb == "list") { return listAction(loaded, table); }
  if (cmd.configVerb == "get") { return getAction(loaded, table, *cmd.configKey); }
  if (cmd.configVerb == "set") {
    return setAction(loaded, configPath, table, *cmd.configKey, *cmd.configValue);
  }
  if (cmd.configVerb == "unset") {
    return unsetAction(loaded, configPath, table, *cmd.configKey);
  }

  // Bare `encro config`: render the config subcommand help (lazily, so it
  // colors against the mode in effect here).
  std::cout << cmd.helpText();
  return 0;
}

}  // namespace cmd
