// Completion emitter: builds a shell-agnostic model from the live CLI11 app
// tree (config injection off) and renders it into bash / PowerShell scripts
// with static candidate data embedded (add-shell-completion design D2/D3/D4).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace completion {

// One completable option in a command scope.
struct OptionInfo {
  std::vector<std::string> names;       // "-r", "--recursive", "--no-recursive"
  std::string id;                       // normalized key used in script tables
  bool takesValue = false;
  std::vector<std::string> candidates;  // registry-captured legal values
  bool numeric = false;
  std::vector<std::string> hiddenBy;    // names: any typed => hide this option
};

struct ScopeInfo {
  std::string name;  // "" for the main command
  std::vector<OptionInfo> options;
};

struct CompletionModel {
  ScopeInfo main;
  std::vector<ScopeInfo> subcommands;  // preview, config, completion
  std::vector<std::string> pathIds;    // value slot -> shell file completion
  std::vector<std::string> configKeys;
  std::map<std::string, std::vector<std::string>> configKeyValues;
};

// Walks a fresh app tree (registered without config injection).
auto buildCompletionModel() -> CompletionModel;

auto emitBashScript(CompletionModel const& model) -> std::string;
auto emitPowerShellScript(CompletionModel const& model) -> std::string;

}  // namespace completion
