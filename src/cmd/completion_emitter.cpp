#include "cmd/completion_emitter.h"

#include "cmd/cmd.h"
#include "cmd/completion_registry.h"
#include "cmd/option_specs.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

namespace completion {

namespace {

// Normalized id for a display name, used as the registry key ("--no-open" ->
// "no_open"; a short-only option gives "-x" -> "_x").
auto normalizedId(std::string name) -> std::string {
  auto id = std::move(name);
  if (id.starts_with("--")) { id.erase(0, 2); }
  std::ranges::replace(id, '-', '_');
  return id;
}

auto displayNames(CLI::Option const* option) -> std::vector<std::string> {
  auto names = std::vector<std::string>{};
  for (auto const& lname: option->get_lnames()) {
    names.push_back(std::format("--{}", lname));
  }
  for (auto const& sname: option->get_snames()) {
    names.push_back(std::format("-{}", sname));
  }
  std::ranges::sort(names);
  return names;
}

// Options of one scope: the app's own options plus those of its nameless
// option-group children (CLI11 nests groups as subcommands; the repo's help
// code relies on the same distinction, cmd.cpp option-group comments). CLI11's
// get_options() also appends the fallthrough parent's options for an app that
// allows fallthrough (the template subcommand does), so those are subtracted:
// they belong to the main scope and would otherwise be offered twice.
auto collectOptionPtrs(CLI::App const& app) -> std::vector<CLI::Option const*> {
  auto ptrs = std::vector<CLI::Option const*>{};
  auto const gather = [&ptrs](CLI::App const& scope) {
    for (auto* option: scope.get_options()) { ptrs.push_back(option); }
  };
  gather(app);
  for (auto const* child: app.get_subcommands([](CLI::App const*) { return true; })) {
    if (child->get_name().empty()) { gather(*child); }
  }

  if (auto const* parent = app.get_parent()) {
    auto const inherited = parent->get_options();
    std::erase_if(ptrs, [&inherited](CLI::Option const* opt) {
      return std::ranges::find(inherited, opt) != inherited.end();
    });
  }
  return ptrs;
}

}  // namespace

auto buildScope(CLI::App const& app, std::string name) -> ScopeInfo {
  auto const ptrs = collectOptionPtrs(app);
  auto namesOf = std::map<CLI::Option const*, std::vector<std::string>>{};
  for (auto const* option: ptrs) {
    auto names = displayNames(option);
    if (!names.empty()) { namesOf[option] = std::move(names); }
  }

  auto scope = ScopeInfo{std::move(name), {}, {}};
  for (auto const* option: ptrs) {
    auto const found = namesOf.find(option);
    if (found == namesOf.end()) {
      // Positional (no display names): candidates and/or a provider id, both
      // keyed by option pointer in the registry.
      auto slot = PositionalInfo{};
      if (auto const* info = positionalInfoOf(option)) { slot = *info; }
      scope.positionals.push_back(std::move(slot));
      continue;
    }
    // Registry data is keyed by long name; a short-only option has none, so
    // its first display name stands in as the id (unique: "-x" -> "_x").
    auto const longName = cfg::captureLongName(option);
    auto info = OptionInfo{};
    info.names = found->second;
    info.id = normalizedId(longName.value_or(found->second.front()));
    info.takesValue = option->get_items_expected_max() > 0;
    if (auto const* value = longName ? valueInfoOf(*longName) : nullptr) {
      info.candidates = value->candidates;
      info.numeric = value->numeric;
      info.provider = value->provider;
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

namespace {

auto sortedUnique(std::vector<std::string> values) -> std::vector<std::string> {
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

// Walks every scope's options once; the id-keyed tables below differ only in
// what they project.
template<typename Project>
void forEachOption(CompletionModel const& model, Project project) {
  auto const walk = [&project](ScopeInfo const& scope) {
    for (auto const& option: scope.options) { project(option); }
  };
  walk(model.main);
  for (auto const& scope: model.subcommands) { walk(scope); }
}

// id -> data, merged across scopes (preview twins carry identical metadata).
auto candidatesById(CompletionModel const& model)
  -> std::map<std::string, std::vector<std::string>> {
  auto merged = std::map<std::string, std::vector<std::string>>{};
  forEachOption(model, [&merged](OptionInfo const& option) {
    if (!option.candidates.empty()) { merged[option.id] = option.candidates; }
  });
  return merged;
}

auto hiddenById(CompletionModel const& model)
  -> std::map<std::string, std::vector<std::string>> {
  auto merged = std::map<std::string, std::vector<std::string>>{};
  forEachOption(model, [&merged](OptionInfo const& option) {
    if (!option.hiddenBy.empty()) { merged[option.id] = option.hiddenBy; }
  });
  return merged;
}

auto valueIds(CompletionModel const& model) -> std::vector<std::string> {
  auto ids = std::vector<std::string>{};
  forEachOption(model, [&ids](OptionInfo const& option) {
    if (option.takesValue) { ids.push_back(option.id); }
  });
  return sortedUnique(std::move(ids));
}

// id -> provider, merged across scopes (a provider-backed option is registered
// once per scope that names it).
auto providersById(CompletionModel const& model) -> std::map<std::string, std::string> {
  auto merged = std::map<std::string, std::string>{};
  forEachOption(model, [&merged](OptionInfo const& option) {
    if (!option.provider.empty()) { merged[option.id] = option.provider; }
  });
  return merged;
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
    if (sub->get_name().empty()) { continue; }   // option groups: folded above
    if (sub->get_group().empty()) { continue; }  // hidden (see help_layout)
    model.subcommands.push_back(buildScope(*sub, sub->get_name()));
  }
  std::ranges::sort(model.subcommands, {}, &ScopeInfo::name);

  for (auto const& longName: pathOptions()) {
    model.pathIds.push_back(normalizedId(longName));
  }
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

  auto const emitScope =
    [&out](std::string const& key, ScopeInfo const& scope, bool isSub) {
      // Plain string (not an array): the glue reads it via ${!opts_var}, which
      // only word-splits simple variables.
      out << "_ENCRO_OPTS_" << key << "=\"" << join(scopeNames(scope)) << "\"\n";
      if (!isSub) { return; }
      out << "_ENCRO_POSN_" << key << "=" << scope.positionals.size() << "\n";
      // Colon-separated per-slot table, omitted entirely when every slot is
      // empty. One text per positional slot; the empty text means "nothing
      // here" to the glue.
      auto const emitSlotTable = [&out,
                                  &key,
                                  &scope](std::string_view prefix, auto const& slotText) {
        auto const any =
          std::ranges::any_of(scope.positionals, [&slotText](PositionalInfo const& slot) {
            return !slotText(slot).empty();
          });
        if (!any) { return; }
        out << prefix << key << "=\"";
        for (std::size_t i = 0; i < scope.positionals.size(); ++i) {
          if (i > 0) { out << ':'; }
          out << slotText(scope.positionals[i]);
        }
        out << "\"\n";
      };
      emitSlotTable("_ENCRO_POSPROV_", [](PositionalInfo const& slot) {
        return slot.provider;
      });
      emitSlotTable("_ENCRO_POSCANDS_", [](PositionalInfo const& slot) {
        return join(slot.candidates);
      });
    };
  emitScope("main", model.main, false);
  for (auto const& scope: model.subcommands) { emitScope(scope.name, scope, true); }

  out << "\n_ENCRO_VALUE_IDS=\" " << join(valueIds(model)) << " \"\n";
  out << "_ENCRO_PATH_IDS=\" " << join(model.pathIds) << " \"\n";
  out << "_ENCRO_SUB_NAMES=\"" << join(sortedUnique(subNames(model))) << "\"\n\n";

  for (auto const& [id, candidates]: candidatesById(model)) {
    out << "_ENCRO_CANDS_" << id << "=\"" << join(candidates) << "\"\n";
  }
  for (auto const& [id, provider]: providersById(model)) {
    out << "_ENCRO_PROV_" << id << "=\"" << provider << "\"\n";
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

# Candidates of a slot backed by live data, resolved by the binary that the
# typed word names. Output is one candidate per line; a text-mode stdout adds a
# CR on Windows, so it is stripped. Returns false when the call fails, which the
# caller turns into "no candidates" and no file fallback.
_encro_slot_from() {
  local out
  out="$("${COMP_WORDS[0]}" completion --candidates "$1" 2>/dev/null)" || return 1
  out="${out//$'\r'/}"
  [ -n "$out" ] || return 1
  COMPREPLY=( $(compgen -W "$out" -- "$cur") )
  [ "${#COMPREPLY[@]}" -gt 0 ]
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
  local scope_idx=0
  for ((i=1; i<COMP_CWORD; i++)); do
    w="${COMP_WORDS[i]}"
    [[ "$w" == -* ]] && continue
    case " $_ENCRO_SUB_NAMES " in *" $w "*) scope="$w"; scope_idx=$i; break ;; esac
  done

  local prev_id="${_ENCRO_NAME_ID[$prev]-}"
  if [ -n "$prev_id" ] && [[ " $_ENCRO_VALUE_IDS " == *" $prev_id "* ]]; then
    if [[ " $_ENCRO_PATH_IDS " == *" $prev_id "* ]]; then
      _encro_files
      return 0
    fi
    local prov_var="_ENCRO_PROV_$prev_id"
    local prov="${!prov_var-}"
    if [ -n "$prov" ]; then
      _encro_slot_from "$prov" || _encro_no_candidates
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

  # config actions are positional verbs: offer the keys after get/set/unset
  # and the key's enumerated values after `set <key>`.
  if [ "$scope" = "config" ] && [[ "$cur" != -* ]]; then
    local action="" vkey=""
    for ((i=scope_idx+1; i<COMP_CWORD; i++)); do
      w="${COMP_WORDS[i]}"
      [[ "$w" == -* ]] && continue
      if [ -z "$action" ]; then action="$w"; continue; fi
      vkey="$w"; break
    done
    if [ "$action" = "set" ] && [ -n "$vkey" ]; then
      local kv_var="_ENCRO_KEY_VALUES_${_ENCRO_KEY_IDS[$vkey]-}"
      COMPREPLY=( $(compgen -W "${!kv_var-}" -- "$cur") )
      [ "${#COMPREPLY[@]}" -eq 0 ] && _encro_no_candidates
      return 0
    fi
    if [ -n "$vkey" ]; then
      _encro_no_candidates
      return 0
    fi
    case "$action" in
      get|set|unset)
        COMPREPLY=( $(compgen -W "$_ENCRO_CONFIG_KEYS" -- "$cur") )
        [ "${#COMPREPLY[@]}" -eq 0 ] && _encro_no_candidates
        return 0 ;;
      list|path)
        _encro_no_candidates
        return 0 ;;
    esac
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

  # Subcommand positional slots: the slot being completed is the count of
  # typed words after the subcommand name that are neither flags nor values
  # of a preceding value-taking option.
  if [ "$scope" != "main" ]; then
    local slot=0 prevw="${COMP_WORDS[scope_idx]}" prevwId
    for ((i=scope_idx+1; i<COMP_CWORD; i++)); do
      w="${COMP_WORDS[i]}"
      if [[ "$w" == -* ]]; then prevw="$w"; continue; fi
      prevwId="${_ENCRO_NAME_ID[$prevw]-}"
      if [ -n "$prevwId" ] && [[ " $_ENCRO_VALUE_IDS " == *" $prevwId "* ]]; then
        prevw="$w"; continue
      fi
      slot=$((slot+1)); prevw="$w"
    done
    local posn_var="_ENCRO_POSN_$scope"
    local posn="${!posn_var:-0}"
    if [ "$slot" -ge "$posn" ]; then
      _encro_no_candidates
      return 0
    fi
    local provs_var="_ENCRO_POSPROV_$scope"
    local provs="${!provs_var-}"
    if [ -n "$provs" ]; then
      local slot_prov
      slot_prov="$(cut -d: -f$((slot+1)) <<<"$provs")"
      if [ -n "$slot_prov" ]; then
        _encro_slot_from "$slot_prov" || _encro_no_candidates
        return 0
      fi
    fi
    local pc_var="_ENCRO_POSCANDS_$scope"
    local pc="${!pc_var-}"
    if [ -n "$pc" ]; then
      local group
      group="$(cut -d: -f$((slot+1)) <<<"$pc")"
      if [ -n "$group" ]; then
        COMPREPLY=( $(compgen -W "$group" -- "$cur") )
        [ "${#COMPREPLY[@]}" -eq 0 ] && _encro_no_candidates
        return 0
      fi
    fi
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

  out << "$__encroPosN = @{\n";
  for (auto const& scope: model.subcommands) {
    out << "  '" << scope.name << "' = " << scope.positionals.size() << "\n";
  }
  out << "}\n";
  // One per-slot table per kind of positional data: the element text per slot,
  // `@()` for a slot with nothing of that kind, and the whole scope omitted
  // when every slot is empty.
  auto const emitPosTable = [&out, &model](std::string_view name, auto const& elementOf) {
    out << "$__encro" << name << " = @{\n";
    for (auto const& scope: model.subcommands) {
      auto const any =
        std::ranges::any_of(scope.positionals, [&elementOf](PositionalInfo const& slot) {
          return !elementOf(slot).empty();
        });
      if (!any) { continue; }
      // Leading comma only for single-slot scopes: without it PowerShell
      // unrolls the single-element outer array when storing, collapsing the
      // per-slot nesting by one level; with it, multi-slot scopes gain a
      // spurious wrapper that shifts every slot index by one level.
      out
        << "  '"
        << scope.name
        << "' = "
        << (scope.positionals.size() == 1 ? ",@(" : "@(");
      for (std::size_t i = 0; i < scope.positionals.size(); ++i) {
        if (i > 0) { out << ", "; }
        auto const element = elementOf(scope.positionals[i]);
        out << (element.empty() ? "@()" : element);
      }
      out << ")\n";
    }
    out << "}\n";
  };
  emitPosTable("PosCands", [&psList](PositionalInfo const& slot) {
    return slot.candidates.empty() ? std::string{} : psList(slot.candidates);
  });
  emitPosTable("PosProvider", [&psList](PositionalInfo const& slot) {
    return slot.provider.empty() ? std::string{} : psList({slot.provider});
  });
  out << "\n";

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
  out << "$__encroProvider = @{\n";
  for (auto const& [id, provider]: providersById(model)) {
    out << "  '" << id << "' = '" << provider << "'\n";
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

  # Candidates of a slot backed by live data, resolved by the binary the typed
  # word names. A missing or failing binary yields no candidates, which the
  # caller turns into the empty-offering self-echo.
  $__encroSlotFrom = {
    param([string] $commandName, [string] $provider, [string] $prefix)
    try {
      $output = @(& $commandName completion --candidates $provider 2>$null)
      if ($LASTEXITCODE -ne 0) { return @() }
    } catch { return @() }
    & $__encroResults @($output | Where-Object { $_ -ne '' }) $prefix
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
    $scopeIdx = -1
    $seenWord = $false
    for ($i = 0; $i -lt $typed.Count; $i++) {
      $w = $typed[$i]
      if (-not $seenWord -and -not $w.StartsWith('-')) {
        $seenWord = $true
        if ($__encroSubNames -contains $w) { $scope = $w; $scopeIdx = $i; break }
      }
    }

    $prevId = if ($__encroNameId.ContainsKey($prev)) { $__encroNameId[$prev] } else { '' }
    if ($prevId -and ($__encroValueIds -contains $prevId)) {
      if ($__encroPathIds -contains $prevId) { return @() }
      if ($__encroProvider.ContainsKey($prevId)) {
        $result = & $__encroSlotFrom $tokens[0] $__encroProvider[$prevId] $wordToComplete
        if ($result.Count -gt 0) { return $result }
        return & $__encroSelf $wordToComplete
      }
      if ($__encroCands.ContainsKey($prevId)) {
        $result = & $__encroResults $__encroCands[$prevId] $wordToComplete
        if ($result.Count -gt 0) { return $result }
      }
      return & $__encroSelf $wordToComplete
    }

    # config actions are positional verbs: offer the keys after get/set/unset
    # and the key's enumerated values after `set <key>`.
    if ($scope -eq 'config' -and -not $cur.StartsWith('-')) {
      $action = ''; $vkey = ''
      for ($i = $scopeIdx + 1; $i -lt $typed.Count; $i++) {
        $w = $typed[$i]
        if ($w.StartsWith('-')) { continue }
        if ($action -eq '') { $action = $w; continue }
        $vkey = $w; break
      }
      if ($action -in 'get', 'set', 'unset') {
        if ($vkey -ne '') {
          if ($action -eq 'set') {
            if ($__encroKeyIds.ContainsKey($vkey)) {
              $keyId = $__encroKeyIds[$vkey]
              if ($__encroKeyValues.ContainsKey($keyId)) {
                $result = & $__encroResults $__encroKeyValues[$keyId] $wordToComplete
                if ($result.Count -gt 0) { return $result }
              }
            }
          }
          return & $__encroSelf $wordToComplete
        }
        return & $__encroResults $__encroConfigKeys $wordToComplete
      }
      if ($action -in 'list', 'path') { return & $__encroSelf $wordToComplete }
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

    # Subcommand positional slots: the slot being completed is the count of
    # typed words after the subcommand name that are neither flags nor values
    # of a preceding value-taking option.
    if ($scope -ne 'main') {
      $slot = 0
      $prevw = $typed[$scopeIdx]
      for ($i = $scopeIdx + 1; $i -lt $typed.Count; $i++) {
        $w = $typed[$i]
        if ($w.StartsWith('-')) { $prevw = $w; continue }
        $prevwId = if ($__encroNameId.ContainsKey($prevw)) { $__encroNameId[$prevw] } else { '' }
        if ($prevwId -and ($__encroValueIds -contains $prevwId)) { $prevw = $w; continue }
        $slot++
        $prevw = $w
      }
      $posn = if ($__encroPosN.ContainsKey($scope)) { $__encroPosN[$scope] } else { 0 }
      if ($slot -ge $posn) { return & $__encroSelf $wordToComplete }
      if ($__encroPosProvider.ContainsKey($scope)) {
        $slotProvider = $__encroPosProvider[$scope][$slot]
        if ($slotProvider) {
          $result = & $__encroSlotFrom $tokens[0] $slotProvider $wordToComplete
          if ($result.Count -gt 0) { return $result }
          return & $__encroSelf $wordToComplete
        }
      }
      if ($__encroPosCands.ContainsKey($scope)) {
        $cands = @($__encroPosCands[$scope][$slot])
        if ($cands.Count -gt 0) {
          $result = & $__encroResults $cands $wordToComplete
          if ($result.Count -gt 0) { return $result }
          return & $__encroSelf $wordToComplete
        }
      }
      return @()
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
