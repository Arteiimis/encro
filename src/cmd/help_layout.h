#pragma once

#include <span>

namespace CLI {
class App;
class Option;
}  // namespace CLI

// Main help: description + usage + commands section + the four option groups
// (the brief tier hides the advanced long names). Installed on the app rather
// than returned as a formatter because CLI11's formatter_fn takes a
// std::function whose signature names CLI-internal types.
auto installHelpFormatter(
  CLI::App& app,
  CLI::App const* general,
  CLI::App const* io,
  CLI::App const* processing,
  CLI::App const* fileop,
  CLI::Option const* helpOpt,
  std::span<CLI::App const* const> subcommands
) -> void;

// Per-subcommand help: the subcommand's own options with its usage lines.
auto installPreviewHelpFormatter(CLI::App& subApp) -> void;
auto installOrganizeHelpFormatter(CLI::App& subApp) -> void;
auto installConfigHelpFormatter(CLI::App& subApp) -> void;
auto installCompletionHelpFormatter(CLI::App& subApp) -> void;
