#include "cmd/completion_emitter.h"

#include "cmd/cmd.h"
#include "cmd/completion_registry.h"
#include "cmd/option_specs.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

namespace completion {

namespace {

auto normalizedId(std::string longName) -> std::string {
  auto id = std::move(longName);  // "--no-open" -> "no_open"
  if (id.starts_with("--")) { id.erase(0, 2); }
  std::ranges::replace(id, '-', '_');
  return id;
}

auto displayNames(CLI::Option const* option) -> std::vector<std::string> {
  auto names = std::vector<std::string>{};
  for (auto const& lname: option->get_lnames()) { names.push_back("--" + lname); }
  for (auto const& sname: option->get_snames()) { names.push_back("-" + sname); }
  std::ranges::sort(names);
  return names;
}

// Options of one scope: the app's own options plus those of its nameless
// option-group children (CLI11 nests groups as subcommands; the repo's help
// code relies on the same distinction, cmd.cpp option-group comments).
auto collectOptionPtrs(CLI::App const& app) -> std::vector<CLI::Option const*> {
  auto ptrs = std::vector<CLI::Option const*>{};
  auto const gather = [&ptrs](CLI::App const& scope) {
    for (auto* option: scope.get_options()) { ptrs.push_back(option); }
  };
  gather(app);
  for (auto const* child: app.get_subcommands([](CLI::App const*) { return true; })) {
    if (child->get_name().empty()) { gather(*child); }
  }
  return ptrs;
}

auto buildScope(CLI::App const& app, std::string name) -> ScopeInfo {
  auto const ptrs = collectOptionPtrs(app);
  auto namesOf = std::map<CLI::Option const*, std::vector<std::string>>{};
  for (auto const* option: ptrs) {
    auto names = displayNames(option);
    if (!names.empty()) { namesOf[option] = std::move(names); }
  }

  auto scope = ScopeInfo{std::move(name), {}};
  for (auto const* option: ptrs) {
    auto const found = namesOf.find(option);
    if (found == namesOf.end()) { continue; }  // positional: shell-native files
    auto const longName = *cfg::captureLongName(option);
    auto info = OptionInfo{};
    info.names = found->second;
    info.id = normalizedId(longName);
    info.takesValue = option->get_items_expected_max() > 0;
    if (auto const* value = valueInfoOf(longName)) {
      info.candidates = value->candidates;
      info.numeric = value->numeric;
    }
    // CLI11 symmetrizes the exclusion graph, so one-sided declarations yield
    // symmetric hiding; positionals contribute no names and are skipped.
    for (auto const* excluded: option->get_excludes()) {
      auto const other = namesOf.find(excluded);
      if (other == namesOf.end()) { continue; }
      for (auto const& hiddenName: other->second) { info.hiddenBy.push_back(hiddenName); }
    }
    std::ranges::sort(info.hiddenBy);
    info.hiddenBy.erase(
      std::unique(info.hiddenBy.begin(), info.hiddenBy.end()),
      info.hiddenBy.end()
    );
    scope.options.push_back(std::move(info));
  }
  std::ranges::sort(scope.options, {}, &OptionInfo::id);
  return scope;
}

auto sortedUnique(std::vector<std::string> values) -> std::vector<std::string> {
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

// id -> data, merged across scopes (preview twins carry identical metadata).
auto candidatesById(CompletionModel const& model)
  -> std::map<std::string, std::vector<std::string>> {
  auto merged = std::map<std::string, std::vector<std::string>>{};
  auto const absorb = [&merged](ScopeInfo const& scope) {
    for (auto const& option: scope.options) {
      if (!option.candidates.empty()) { merged[option.id] = option.candidates; }
    }
  };
  absorb(model.main);
  for (auto const& scope: model.subcommands) { absorb(scope); }
  return merged;
}

auto hiddenById(CompletionModel const& model)
  -> std::map<std::string, std::vector<std::string>> {
  auto merged = std::map<std::string, std::vector<std::string>>{};
  auto const absorb = [&merged](ScopeInfo const& scope) {
    for (auto const& option: scope.options) {
      if (!option.hiddenBy.empty()) { merged[option.id] = option.hiddenBy; }
    }
  };
  absorb(model.main);
  for (auto const& scope: model.subcommands) { absorb(scope); }
  return merged;
}

auto valueIds(CompletionModel const& model) -> std::vector<std::string> {
  auto ids = std::vector<std::string>{};
  auto const absorb = [&ids](ScopeInfo const& scope) {
    for (auto const& option: scope.options) {
      if (option.takesValue) { ids.push_back(option.id); }
    }
  };
  absorb(model.main);
  for (auto const& scope: model.subcommands) { absorb(scope); }
  return sortedUnique(std::move(ids));
}

auto scopeNames(ScopeInfo const& scope) -> std::vector<std::string> {
  auto names = std::vector<std::string>{};
  for (auto const& option: scope.options) {
    for (auto const& name: option.names) { names.push_back(name); }
  }
  return sortedUnique(std::move(names));
}

auto subNames(CompletionModel const& model) -> std::vector<std::string> {
  auto names = std::vector<std::string>{};
  for (auto const& scope: model.subcommands) { names.push_back(scope.name); }
  return names;
}

auto join(std::vector<std::string> const& values, char sep = ' ') -> std::string {
  auto out = std::ostringstream{};
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) { out << sep; }
    out << values[i];
  }
  return out.str();
}

}  // namespace

auto buildCompletionModel() -> CompletionModel {
  auto result = CmdParseResult{};
  auto tree = buildAppTree(result, "encro completion model", false);

  auto model = CompletionModel{};
  model.main = buildScope(*tree.app, "");
  for (auto* sub: tree.app->get_subcommands([](CLI::App*) { return true; })) {
    if (sub->get_name().empty()) { continue; }  // option groups: folded above
    model.subcommands.push_back(buildScope(*sub, sub->get_name()));
  }
  std::ranges::sort(model.subcommands, {}, &ScopeInfo::name);

  model.pathIds = {"input", "inputs", "output", "state_file", "ffmpeg_path"};
  for (auto const& [key, longName]: configKeyOptions()) {
    model.configKeys.push_back(key);
    if (
      auto const* value = valueInfoOf(longName);
      value != nullptr && !value->candidates.empty()
    ) {
      model.configKeyValues[key] = value->candidates;
    }
  }
  return model;
}

auto emitBashScript(CompletionModel const& model) -> std::string {
  auto out = std::ostringstream{};
  out
    << "# encro bash completion. Generated by `encro completion bash`; do not edit.\n"
    << "# Re-run after upgrading encro, or reinstall:\n"
    << "#   encro completion bash --install\n\n";

  auto nameId = std::map<std::string, std::string>{};
  auto const absorbNames = [&nameId](ScopeInfo const& scope) {
    for (auto const& option: scope.options) {
      for (auto const& optionName: option.names) { nameId[optionName] = option.id; }
    }
  };
  absorbNames(model.main);
  for (auto const& scope: model.subcommands) { absorbNames(scope); }

  out << "declare -A _ENCRO_NAME_ID=(\n";
  for (auto const& [optionName, id]: nameId) {
    out << "  [\"" << optionName << "\"]=" << id << "\n";
  }
  out << ")\n\n";

  auto const emitScope = [&out](std::string const& key, ScopeInfo const& scope) {
    // Plain string (not an array): the glue reads it via ${!opts_var}, which
    // only word-splits simple variables.
    out << "_ENCRO_OPTS_" << key << "=\"" << join(scopeNames(scope)) << "\"\n";
  };
  emitScope("main", model.main);
  for (auto const& scope: model.subcommands) { emitScope(scope.name, scope); }

  out << "\n_ENCRO_VALUE_IDS=\" " << join(valueIds(model)) << " \"\n";
  out << "_ENCRO_PATH_IDS=\" " << join(model.pathIds) << " \"\n";
  out << "_ENCRO_SUB_NAMES=\"" << join(sortedUnique(subNames(model))) << "\"\n\n";

  for (auto const& [id, candidates]: candidatesById(model)) {
    out << "_ENCRO_CANDS_" << id << "=\"" << join(candidates) << "\"\n";
  }
  out << "\n";
  for (auto const& [id, hidden]: hiddenById(model)) {
    out << "_ENCRO_HIDDEN_" << id << "=\"" << join(hidden) << "\"\n";
  }

  auto keyIds = std::map<std::string, std::string>{};
  for (auto const& key: model.configKeys) { keyIds[key] = normalizedId(key); }
  out << "\n_ENCRO_CONFIG_KEYS=\"" << join(model.configKeys) << "\"\n";
  if (!keyIds.empty()) {
    out << "declare -A _ENCRO_KEY_IDS=(\n";
    for (auto const& [key, id]: keyIds) { out << "  [\"" << key << "\"]=" << id << "\n"; }
    out << ")\n";
    for (auto const& [key, candidates]: model.configKeyValues) {
      out
        << "_ENCRO_KEY_VALUES_"
        << normalizedId(key)
        << "=\""
        << join(candidates)
        << "\"\n";
    }
  }

  out << R"GLUE(
_encro_files() {
  COMPREPLY=( $(compgen -A file -- "$cur") )
}

# Suppress the `-o default` file fallback for slots that must offer nothing.
_encro_no_candidates() {
  compopt +o default 2>/dev/null
  COMPREPLY=()
}

_encro_complete() {
  local cur prev i w
  COMPREPLY=()
  cur="${COMP_WORDS[COMP_CWORD]}"
  prev="${COMP_WORDS[COMP_CWORD-1]}"
  local typed=" ${COMP_WORDS[*]:1:COMP_CWORD-1} "

  local scope="main"
  for ((i=1; i<COMP_CWORD; i++)); do
    w="${COMP_WORDS[i]}"
    [[ "$w" == -* ]] && continue
    case " $_ENCRO_SUB_NAMES " in *" $w "*) scope="$w"; break ;; esac
  done

  local prev_id="${_ENCRO_NAME_ID[$prev]-}"
  if [ -n "$prev_id" ] && [[ " $_ENCRO_VALUE_IDS " == *" $prev_id "* ]]; then
    if [[ " $_ENCRO_PATH_IDS " == *" $prev_id "* ]]; then
      _encro_files
      return 0
    fi
    if [ "$prev_id" = "set" ] && [ "$scope" = "config" ]; then
      COMPREPLY=( $(compgen -W "$_ENCRO_CONFIG_KEYS" -- "$cur") )
      [ "${#COMPREPLY[@]}" -eq 0 ] && _encro_no_candidates
      return 0
    fi
    local cands_var="_ENCRO_CANDS_$prev_id"
    local cands="${!cands_var-}"
    if [ -n "$cands" ]; then
      COMPREPLY=( $(compgen -W "$cands" -- "$cur") )
      [ "${#COMPREPLY[@]}" -eq 0 ] && _encro_no_candidates
      return 0
    fi
    _encro_no_candidates
    return 0
  fi

  if [ "$scope" = "config" ]; then
    local set_idx=-1
    for ((i=1; i<COMP_CWORD; i++)); do
      [ "${COMP_WORDS[i]}" = "--set" ] && set_idx=$i
    done
    if [ "$set_idx" -ge 0 ] && [ $((COMP_CWORD - set_idx - 1)) -ge 1 ] && [[ "$cur" != -* ]]; then
      local key="${COMP_WORDS[set_idx+1]}"
      local key_id="${_ENCRO_KEY_IDS[$key]-}"
      local kv_var="_ENCRO_KEY_VALUES_${key_id}"
      local kv="${!kv_var-}"
      if [ -n "$kv" ]; then
        COMPREPLY=( $(compgen -W "$kv" -- "$cur") )
        [ "${#COMPREPLY[@]}" -eq 0 ] && _encro_no_candidates
      else
        _encro_no_candidates
      fi
      return 0
    fi
  fi

  if [[ "$cur" == -* ]]; then
    local opts_var="_ENCRO_OPTS_$scope"
    local out=() o oid hid_var h skip
    for o in ${!opts_var}; do
      [[ "$o" == "$cur"* ]] || continue
      oid="${_ENCRO_NAME_ID[$o]-}"
      skip=0
      if [ -n "$oid" ]; then
        hid_var="_ENCRO_HIDDEN_$oid"
        for h in ${!hid_var-}; do
          case "$typed" in *" $h "*) skip=1; break ;; esac
        done
      fi
      [ "$skip" = 1 ] || out+=("$o")
    done
    COMPREPLY=( "${out[@]}" )
    return 0
  fi

  if [ "$scope" = "main" ]; then
    COMPREPLY=( $(compgen -W "$_ENCRO_SUB_NAMES" -- "$cur") )
  fi
  return 0
}
complete -o default -F _encro_complete encro
)GLUE";
  return out.str();
}

auto emitPowerShellScript(CompletionModel const& model) -> std::string {
  auto out = std::ostringstream{};
  out
    << "# encro PowerShell completion. Generated by `encro completion powershell`; do "
       "not edit.\n"
    << "# Re-run after upgrading encro, or reinstall:\n"
    << "#   encro completion powershell --install\n\n";

  auto nameId = std::map<std::string, std::string>{};
  auto const absorbNames = [&nameId](ScopeInfo const& scope) {
    for (auto const& option: scope.options) {
      for (auto const& optionName: option.names) { nameId[optionName] = option.id; }
    }
  };
  absorbNames(model.main);
  for (auto const& scope: model.subcommands) { absorbNames(scope); }

  auto const psList = [](std::vector<std::string> const& values) -> std::string {
    auto joined = std::ostringstream{};
    joined << "@(";
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (i > 0) { joined << ", "; }
      joined << '\'' << values[i] << '\'';
    }
    joined << ")";
    return joined.str();
  };

  // Case-sensitive dictionary: PowerShell hash literals are case-insensitive
  // and short names collide (-F vs -f).
  out
    << "$__encroNameId = [System.Collections.Generic.Dictionary[string,string]]::new(\n"
    << "  [System.StringComparer]::Ordinal)\n";
  for (auto const& [optionName, id]: nameId) {
    out << "$__encroNameId['" << optionName << "'] = '" << id << "'\n";
  }
  out << "\n";

  out << "$__encroOpts = @{\n";
  out << "  'main' = " << psList(scopeNames(model.main)) << "\n";
  for (auto const& scope: model.subcommands) {
    out << "  '" << scope.name << "' = " << psList(scopeNames(scope)) << "\n";
  }
  out << "}\n\n";

  out << "$__encroValueIds = " << psList(valueIds(model)) << "\n";
  out << "$__encroPathIds = " << psList(model.pathIds) << "\n";
  out << "$__encroSubNames = " << psList(sortedUnique(subNames(model))) << "\n";

  out << "$__encroCands = @{\n";
  for (auto const& [id, candidates]: candidatesById(model)) {
    out << "  '" << id << "' = " << psList(candidates) << "\n";
  }
  out << "}\n";
  out << "$__encroHidden = @{\n";
  for (auto const& [id, hidden]: hiddenById(model)) {
    out << "  '" << id << "' = " << psList(hidden) << "\n";
  }
  out << "}\n";

  out << "$__encroConfigKeys = " << psList(model.configKeys) << "\n";
  out << "$__encroKeyIds = @{\n";
  for (auto const& key: model.configKeys) {
    out << "  '" << key << "' = '" << normalizedId(key) << "'\n";
  }
  out << "}\n";
  out << "$__encroKeyValues = @{\n";
  for (auto const& [key, candidates]: model.configKeyValues) {
    out << "  '" << normalizedId(key) << "' = " << psList(candidates) << "\n";
  }
  out << "}\n\n";

  out << R"GLUE($__encroResults = {
    param([string[]] $matches, [string] $prefix)
    @($matches | Where-Object { $_ -like "$prefix*" } | ForEach-Object {
      [System.Management.Automation.CompletionResult]::new($_, $_, 'Text', $_)
    })
  }

  # A slot that must offer nothing: an empty return would fall back to native
  # file completion (spike-verified), so echo the typed word back instead.
  $__encroSelf = {
    param([string] $prefix)
    # Empty text would be dropped by the engine (re-exposing file fallback),
    # so an empty prefix echoes a single space instead.
    $text = if ($prefix -eq '') { ' ' } else { $prefix }
    [System.Management.Automation.CompletionResult]::new($text, $text, 'Text', $text)
  }

  Register-ArgumentCompleter -CommandName encro -Native -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)
    # $wordToComplete is authoritative for the word being completed; the AST
    # text drops trailing spaces, so never re-derive $cur from it.
    $tokens = @($commandAst.ToString() -split '\s+' | Where-Object { $_ -ne '' })
    if ($tokens.Count -eq 0) { return @() }
    $typed = @($tokens | Select-Object -Skip 1)
    if ($typed.Count -gt 0 -and $typed[-1] -eq $wordToComplete -and $wordToComplete -ne '') {
      $typed = @($typed | Select-Object -First ($typed.Count - 1))
    }
    $cur = $wordToComplete
    $prev = if ($typed.Count -gt 0) { $typed[-1] } else { '' }

    $scope = 'main'
    $seenWord = $false
    foreach ($w in $typed) {
      if (-not $seenWord -and -not $w.StartsWith('-')) {
        $seenWord = $true
        if ($__encroSubNames -contains $w) { $scope = $w }
      }
    }

    $prevId = if ($__encroNameId.ContainsKey($prev)) { $__encroNameId[$prev] } else { '' }
    if ($prevId -and ($__encroValueIds -contains $prevId)) {
      if ($__encroPathIds -contains $prevId) { return @() }
      if ($prevId -eq 'set' -and $scope -eq 'config') {
        return & $__encroResults $__encroConfigKeys $wordToComplete
      }
      if ($__encroCands.ContainsKey($prevId)) {
        $result = & $__encroResults $__encroCands[$prevId] $wordToComplete
        if ($result.Count -gt 0) { return $result }
      }
      return & $__encroSelf $wordToComplete
    }

    if ($scope -eq 'config') {
      $setIdx = -1
      for ($i = 0; $i -lt $typed.Count; $i++) {
        if ($typed[$i] -eq '--set') { $setIdx = $i }
      }
      if ($setIdx -ge 0 -and ($typed.Count - $setIdx - 1) -ge 1 -and -not $cur.StartsWith('-')) {
        $key = $typed[$setIdx + 1]
        if ($__encroKeyIds.ContainsKey($key)) {
          $keyId = $__encroKeyIds[$key]
          if ($__encroKeyValues.ContainsKey($keyId)) {
            $result = & $__encroResults $__encroKeyValues[$keyId] $wordToComplete
            if ($result.Count -gt 0) { return $result }
          }
        }
        return & $__encroSelf $wordToComplete
      }
    }

    if ($cur.StartsWith('-')) {
      $out = @()
      foreach ($o in $__encroOpts[$scope]) {
        if (-not $o.StartsWith($cur)) { continue }
        $oid = $__encroNameId[$o]
        if ($oid -and $__encroHidden.ContainsKey($oid)) {
          $hide = $false
          foreach ($h in $__encroHidden[$oid]) {
            if ($typed -contains $h) { $hide = $true; break }
          }
          if ($hide) { continue }
        }
        $out += [System.Management.Automation.CompletionResult]::new($o, $o, 'Text', $o)
      }
      return $out
    }

    if ($scope -eq 'main') {
      return & $__encroResults $__encroSubNames $wordToComplete
    }
    return @()
  }
)GLUE";
  return out.str();
}

auto scriptFor(std::string const& shell) -> std::optional<std::string> {
  auto const model = buildCompletionModel();
  if (shell == "bash") { return emitBashScript(model); }
  if (shell == "powershell") { return emitPowerShellScript(model); }
  return std::nullopt;
}

}  // namespace completion
