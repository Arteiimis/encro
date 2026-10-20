#include "cmd/template_command.h"

#include "cmd/config_store.h"
#include "cmd/template_store.h"
#include "core/path_utf8.h"
#include "infra/terminal.h"

#include <cstdio>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace cmd {

using enum terminal::MessageKind;

namespace {

int reportError(std::string_view message) {
  terminal::eprintln(Error, "{}", message);
  return 1;
}

// Byte-exact stdout write (no added newline), through C stdio: `show` and
// `schema` print a file's content verbatim, and stdio is the stream the
// console redirects target (std::cout's own buffer escapes that redirect).
void writeStdout(std::string const& text) {
  std::fwrite(text.data(), 1, text.size(), stdout);
  std::fflush(stdout);
}

// One stored key in CLI form: a boolean reads as the flag that sets it, so a
// list line can be retyped on a command line.
auto cliForm(configstore::KeyDef const& def, std::string const& value) -> std::string {
  if (def.kind == configstore::JsonKind::Boolean) {
    return value == "true" ? std::format("--{}", def.key)
                           : std::format("--no-{}", def.key);
  }
  return std::format("--{} {}", def.key, value);
}

int listAction(fs::path const& dir, configstore::KeyTable const& table) {
  auto const names = templatestore::list(dir);
  if (names.empty()) {
    terminal::println(
      Plain,
      "no templates yet: store one with 'encro template save <name> [options]'"
    );
    return 0;
  }

  for (auto const& name: names) {
    auto const loaded = templatestore::load(dir / (name + ".json"), table);
    if (loaded.error) { return reportError(*loaded.error); }

    auto summary = std::string{};
    for (auto const& def: table.keys) {
      auto const it = loaded.values.find(std::string{def.key});
      if (it == loaded.values.end()) { continue; }
      if (!summary.empty()) { summary += ' '; }
      summary += cliForm(def, it->second);
    }
    terminal::println(Plain, "{:<16} {}", name, summary);
  }
  return 0;
}

int showAction(fs::path const& path) {
  auto file = std::ifstream{path, std::ios::binary};
  if (!file.is_open()) {
    return reportError(
      std::format("cannot read template file: {}", pathutf8::fromPath(path))
    );
  }
  auto const content = std::string{std::istreambuf_iterator<char>{file}, {}};
  writeStdout(content);
  return 0;
}

int deleteAction(fs::path const& dir, std::string const& name) {
  auto const path = templatestore::find(dir, name);
  if (!path.has_value()) {
    return reportError(templatestore::unknownTemplateMessage(name));
  }

  auto ec = std::error_code{};
  fs::remove(*path, ec);
  if (ec) {
    return reportError(
      std::format(
        "cannot delete template file {}: {}",
        pathutf8::fromPath(*path),
        ec.message()
      )
    );
  }
  return 0;
}

int unsetAction(
  fs::path const& dir,
  configstore::KeyTable const& table,
  std::string const& name,
  std::string const& key
) {
  if (table.find(key) == nullptr) {
    return reportError(
      std::format(
        "unknown template key: {} (run 'encro template show {}' for the keys it "
        "stores)",
        key,
        name
      )
    );
  }

  auto const path = templatestore::find(dir, name);
  if (!path.has_value()) {
    return reportError(templatestore::unknownTemplateMessage(name));
  }

  auto loaded = templatestore::load(*path, table);
  if (loaded.error) { return reportError(*loaded.error); }
  // Absent key: nothing to rewrite, same exit code as config's unset.
  if (loaded.values.erase(key) == 0) { return 0; }

  auto const error =
    templatestore::save(*path, loaded.values, loaded.unknownValues, table);
  return error.has_value() ? reportError(*error) : 0;
}

int saveAction(
  fs::path const& dir,
  configstore::KeyTable const& table,
  std::string const& name,
  std::map<std::string, std::string> const& captured
) {
  // An existing template is merged into, so a short save cannot silently
  // truncate one; a new name gets the canonical path.
  auto const existing = templatestore::find(dir, name);
  auto const path = existing.value_or(dir / (name + ".json"));

  auto loaded = templatestore::load(path, table);
  if (loaded.error) { return reportError(*loaded.error); }
  for (auto const& [key, value]: captured) { loaded.values.insert_or_assign(key, value); }

  if (
    auto const error =
      templatestore::save(path, loaded.values, loaded.unknownValues, table);
    error.has_value()
  ) {
    return reportError(*error);
  }
  if (auto const error = templatestore::writeSchema(dir, table); error.has_value()) {
    return reportError(*error);
  }

  auto const count = loaded.values.size();
  terminal::println(
    Plain,
    "saved template {} ({} key{}) to {}",
    name,
    count,
    count == 1 ? "" : "s",
    terminal::path(path)
  );
  return 0;
}

}  // namespace

int runTemplateCommand(CmdParseResult const& cmd) {
  auto const dir = templatestore::resolveTemplatesDir();
  auto const& table = cmd.templateTable;

  // `path` and `schema` need no stored template and never read one.
  if (cmd.templateVerb == "path") {
    terminal::println(Plain, "{}", terminal::path(dir));
    return 0;
  }
  if (cmd.templateVerb == "schema") {
    writeStdout(templatestore::schema(table));
    return 0;
  }
  if (cmd.templateVerb == "list") { return listAction(dir, table); }

  // Bare `encro template`: render the subcommand help (lazily, so it colors
  // against the mode in effect here).
  if (cmd.templateVerb.empty()) {
    writeStdout(cmd.helpText());
    return 0;
  }

  // The arity validation in cmd.cpp guarantees a name for show/save/unset/
  // delete.
  auto const rawName = cmd.templateName.value_or(std::string{});
  auto const name = templatestore::normalizeName(rawName);
  if (name.empty()) {
    return reportError(
      std::format(
        "invalid template name: {} (lowercase letters, digits, '.', '_' and '-', "
        "starting with a letter or digit)",
        rawName
      )
    );
  }

  if (cmd.templateVerb == "show") {
    auto const path = templatestore::find(dir, name);
    if (!path.has_value()) {
      return reportError(templatestore::unknownTemplateMessage(name));
    }
    return showAction(*path);
  }
  if (cmd.templateVerb == "delete") { return deleteAction(dir, name); }
  if (cmd.templateVerb == "unset") {
    return unsetAction(dir, table, name, cmd.templateKey.value_or(std::string{}));
  }
  if (cmd.templateVerb == "save") {
    return saveAction(dir, table, name, cmd.capturedTemplateValues);
  }

  // Unreachable while cfg::Members gates the verbs; kept so a verb added there
  // without a handler fails loudly instead of exiting 0.
  return reportError(std::format("unknown template action: {}", cmd.templateVerb));
}

}  // namespace cmd
