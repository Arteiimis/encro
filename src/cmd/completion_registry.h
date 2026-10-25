// Completion-value registry: metadata captured from the declarative option
// tokens at registration time (add-shell-completion design D2). Candidates
// come from Members/CheckedTransformer, the numeric marker from the number
// validators, and config keys map to their options' long names. Path options
// are long-name keyed; positional metadata is keyed by option pointer
// (positionals have no long name; the app tree is leaked on purpose so the
// pointers stay valid). A slot can instead be backed by a runtime provider,
// whose id the emitted script resolves at completion time
// (complete-template-names design D3/D4).
#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace CLI {

class Option;

}

namespace completion {

// Dynamic candidate source ids: one constant shared by the registration token
// and the provider dispatch, so a typo cannot become a silent no-op.
inline constexpr std::string_view kTemplateNameProvider = "template-name";

struct ValueInfo {
  std::vector<std::string> candidates;  // enumerated legal values, canonical
  bool numeric = false;                 // number-valued: never offers candidates
  std::string provider;                 // runtime candidate source (empty: static)
};

// One positional slot's captured metadata; every field is optional, and an
// empty slot delegates to the shell's native file completion.
struct PositionalInfo {
  std::vector<std::string> candidates;
  std::string provider;  // runtime candidate source (empty: static)
};

// Process-global maps keyed by canonical long name ("--crf"). insert_or_assign
// capture keeps repeated app builds idempotent, mirroring the config-key
// registry in configstore.
auto optionValues() -> std::map<std::string, ValueInfo>&;
auto configKeyOptions() -> std::map<std::string, std::string>&;
auto pathOptions() -> std::set<std::string>&;
auto positionals() -> std::map<CLI::Option const*, PositionalInfo>&;

void recordCandidates(std::string longName, std::vector<std::string> values);
void recordNumeric(std::string longName);
void recordConfigKey(std::string_view key, std::string longName);
void recordPath(std::string longName);
void recordPositional(CLI::Option const* option, std::vector<std::string> values);
void recordProvider(std::string longName, std::string_view provider);
void recordPositionalProvider(CLI::Option const* option, std::string_view provider);

// nullptr when the option captured nothing (flags, free text, paths)
auto valueInfoOf(std::string const& longName) -> ValueInfo const*;

// Keys in map (sorted) order
auto configKeys() -> std::vector<std::string>;

// nullptr when the key is unknown
auto longNameOfConfigKey(std::string const& key) -> std::string const*;

// nullptr when the positional captured no metadata
auto positionalInfoOf(CLI::Option const* option) -> PositionalInfo const*;

}  // namespace completion
