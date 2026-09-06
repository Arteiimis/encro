// Completion-value registry: metadata captured from the declarative option
// tokens at registration time (add-shell-completion design D2). Candidates
// come from Members/CheckedTransformer, the numeric marker from the number
// validators, and config keys map to their options' long names. Path options
// are long-name keyed; positional candidates are keyed by option pointer
// (positionals have no long name; the app tree is leaked on purpose so the
// pointers stay valid, mirroring the config-store capture).
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

struct ValueInfo {
  std::vector<std::string> candidates;  // enumerated legal values, canonical
  bool numeric = false;                 // number-valued: never offers candidates
};

// Process-global maps keyed by canonical long name ("--crf"). insert_or_assign
// capture keeps repeated app builds idempotent, mirroring the config-key
// registry in configstore.
auto optionValues() -> std::map<std::string, ValueInfo>&;
auto configKeyOptions() -> std::map<std::string, std::string>&;
auto pathOptions() -> std::set<std::string>&;
auto positionalOptions() -> std::map<CLI::Option const*, std::vector<std::string>>&;

void recordCandidates(std::string longName, std::vector<std::string> values);
void recordNumeric(std::string longName);
void recordConfigKey(std::string_view key, std::string longName);
void recordPath(std::string longName);
void recordPositional(CLI::Option const* option, std::vector<std::string> values);

// nullptr when the option captured nothing (flags, free text, paths)
auto valueInfoOf(std::string const& longName) -> ValueInfo const*;

// Keys in map (sorted) order
auto configKeys() -> std::vector<std::string>;

// nullptr when the key is unknown
auto longNameOfConfigKey(std::string const& key) -> std::string const*;

// nullptr when the positional captured no enumerated values
auto positionalCandidatesOf(CLI::Option const* option) -> std::vector<std::string> const*;

}  // namespace completion
