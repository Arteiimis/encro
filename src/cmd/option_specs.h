// Declarative CLI11 option registration: an option is a single OptSpec
// (name, bound field, description) plus a variadic list of cfg tokens that
// configure the option. Registration happens in two phases — phase 1 binds
// and configures every option, phase 2 resolves Excludes/Needs by pointer
// (option names like "-p,--pack" are not resolvable via get_option).
#pragma once

#include "cmd/completion_registry.h"
#include "cmd/config_store.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace cfg {

// Canonical long name with dashes ("--x"), used as the completion-registry
// key; nullopt for positionals (they have no long name to complete).
inline auto captureLongName(CLI::Option const* option) -> std::optional<std::string> {
  if (option->get_lnames().empty()) { return std::nullopt; }
  return "--" + option->get_lnames().front();
}

struct OptionalDefault {
  std::string value;
  void operator()(CLI::Option* option) const {
    option->expected(0, 1)->default_str(value);
  }
};

struct RequiredDefault {
  std::string value;
  void operator()(CLI::Option* option) const { option->expected(1)->default_str(value); }
};

struct DefaultValue {
  std::string value;
  void operator()(CLI::Option* option) const { option->default_str(value); }
};

struct Expected {
  int lo;
  int hi;
  void operator()(CLI::Option* option) const { option->expected(lo, hi); }
};

struct Range {
  int lo;
  int hi;
  void operator()(CLI::Option* option) const {
    option->check(CLI::Range(lo, hi));
    if (auto const name = captureLongName(option)) { completion::recordNumeric(*name); }
  }
};

// Double-bounded range for floating-point options (Range is int-only).
struct FloatRange {
  double lo;
  double hi;
  void operator()(CLI::Option* option) const { option->check(CLI::Range(lo, hi)); }
};

struct PositiveNumber {
  void operator()(CLI::Option* option) const {
    option->check(CLI::PositiveNumber);
    if (auto const name = captureLongName(option)) { completion::recordNumeric(*name); }
  }
};

struct NonNegativeNumber {
  void operator()(CLI::Option* option) const {
    option->check(CLI::NonNegativeNumber);
    if (auto const name = captureLongName(option)) { completion::recordNumeric(*name); }
  }
};

// Marks the option's value as a file/directory path: completion delegates the
// value slot to the shell's native file-name completion.
struct Path {
  void operator()(CLI::Option* option) const {
    if (auto const name = captureLongName(option)) { completion::recordPath(*name); }
  }
};

struct Required {
  void operator()(CLI::Option* option) const { option->required(); }
};

struct Members {
  std::vector<std::string> legal;
  // non-aggregate on purpose: lets the legal values be written as a flat
  // initializer list {"a", "b"} instead of nested vector braces
  Members(std::initializer_list<std::string> values): legal(values) { }
  void operator()(CLI::Option* option) const {
    option->check(CLI::IsMember(legal));
    if (option->get_lnames().empty() && option->get_snames().empty()) {
      // Positional (no long/short names): captured by option pointer so the
      // emitter can resolve it while walking the scope's positional order.
      completion::recordPositional(option, legal);
    }
    if (auto const name = captureLongName(option)) {
      completion::recordCandidates(*name, legal);
    }
  }
};

struct CheckedTransformer {
  std::vector<std::pair<std::string, std::string>> mapping;
  // non-aggregate on purpose: lets the mapping be written as a flat pair list
  // {{a, b}, {c, d}} like upstream CheckedTransformer
  CheckedTransformer(std::initializer_list<std::pair<std::string, std::string>> values)
    : mapping(values) { }
  void operator()(CLI::Option* option) const {
    option->transform(CLI::CheckedTransformer(mapping));
    // completion offers the canonical (target) values, first occurrence wins
    auto canonical = std::vector<std::string>{};
    for (auto const& [from, to]: mapping) {
      if (std::ranges::find(canonical, to) == canonical.end()) {
        canonical.push_back(to);
      }
    }
    if (auto const name = captureLongName(option)) {
      completion::recordCandidates(*name, std::move(canonical));
    }
  }
};

struct Transform {
  std::function<std::string(std::string)> fn;
  void operator()(CLI::Option* option) const { option->transform(fn); }
};

struct Excludes {
  std::string other;  // long name of the excluded option, resolved in phase 2
  void operator()(CLI::Option*) const { }  // handled declaratively in applyDeps
};

struct Needs {
  std::string other;  // long name of the required option, resolved in phase 2
  void operator()(CLI::Option*) const { }  // handled declaratively in applyDeps
};

// Marks the option as the CLI surface of a config key: registerOne records the
// key's table entry (kind, long name, built-in default, validators) while the
// option is registered. The token carries the name only (design D2).
struct ConfigKey {
  std::string_view name;
  void operator()(CLI::Option*) const { }
};

// JSON kind of a binding type (design D2): a flag is a boolean (checked first,
// std::is_arithmetic_v<bool> is true), arithmetic types and std::optional of
// one are numbers, everything else is a string.
template<typename Ty>
struct IsArithmeticBinding: std::is_arithmetic<std::remove_cvref_t<Ty>> { };

template<typename Ty>
struct IsArithmeticBinding<std::optional<Ty>>: std::is_arithmetic<Ty> { };

template<typename Ty>
constexpr auto jsonKindFor() -> configstore::JsonKind {
  using Value = std::remove_cvref_t<Ty>;
  if constexpr (std::is_same_v<Value, bool>) {
    return configstore::JsonKind::Boolean;
  } else if constexpr (IsArithmeticBinding<Ty>::value) {
    return configstore::JsonKind::Number;
  } else {
    return configstore::JsonKind::String;
  }
}

// Name of the spec's cfg::ConfigKey token, empty when it carries none.
template<typename... Cfg>
constexpr auto configKeyName(std::tuple<Cfg...> const& cfg) -> std::string_view {
  auto name = std::string_view{};
  std::apply(
    [&name](auto const&... cfgItems) {
      auto const take = [&name](auto const& item) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(item)>, ConfigKey>) {
          name = item.name;
        }
      };
      (take(cfgItems), ...);
    },
    cfg
  );
  return name;
}

}  // namespace cfg

template<typename Ty, typename... Cfg>
struct OptSpec {
  using binding_type = Ty;
  std::string name;
  Ty* binding;
  std::string desc;
  std::tuple<Cfg...> cfg;
};

template<typename Ty, typename... Cfg>
auto opt(std::string name, Ty* binding, std::string desc, Cfg... cfg)
  -> OptSpec<Ty, Cfg...> {
  return {std::move(name), binding, std::move(desc), std::make_tuple(std::move(cfg)...)};
}

// Copy of the option's validator chain in run order (CLI11 inserts transforms
// at the front, appends checks). `validators_` is protected with no public
// getter, so the walk goes through get_validator(index) until it reports
// OptionNotFound. The copies are self-contained -- that is what lets a table
// entry outlive the option tree (design D1).
inline void appendValidators(CLI::Option* option, std::vector<CLI::Validator>& out) {
  for (auto index = 0;; ++index) {
    try {
      out.push_back(*option->get_validator(index));
    } catch (CLI::OptionNotFound const&) { return; }
  }
}

// Phase 1: register and configure one option. bool bindings are flags,
// everything else takes a value. A cfg::ConfigKey token also records the
// option's table entry, after the whole cfg tuple has run: the validators the
// spec's later tokens add are part of the key's contract.
template<typename Spec>
auto registerOne(
  CLI::App* app,
  Spec const& spec,
  std::vector<configstore::KeyDef>& entries
) -> CLI::Option* {
  auto* option = [&]() -> CLI::Option* {
    if constexpr (
      std::is_same_v<std::remove_cvref_t<typename Spec::binding_type>, bool>
    ) {
      return app->add_flag(spec.name, *spec.binding, spec.desc);
    } else {
      return app->add_option(spec.name, *spec.binding, spec.desc);
    }
  }();
  std::apply([&](auto const&... cfgItems) { (cfgItems(option), ...); }, spec.cfg);

  if (auto const key = cfg::configKeyName(spec.cfg); !key.empty()) {
    auto def = configstore::KeyDef{
      .key = key,
      .kind = cfg::jsonKindFor<typename Spec::binding_type>(),
      .longName = cfg::captureLongName(option).value_or(std::string{}),
      .builtinDefault = option->get_default_str(),
    };
    appendValidators(option, def.validators);
    entries.push_back(std::move(def));
  }
  return option;
}

// Phase 2: resolve Excludes/Needs by long name against the same app scope.
template<typename Spec>
void applyDeps(CLI::App* app, CLI::Option* self, Spec const& spec) {
  std::apply(
    [&](auto const&... cfgItems) {
      auto const applyOne = [&](auto const& item) {
        using Ty = std::remove_cvref_t<decltype(item)>;
        if constexpr (
          std::is_same_v<Ty, cfg::Excludes> || std::is_same_v<Ty, cfg::Needs>
        ) {
          auto* other = app->get_option_no_throw(item.other);
          if (other != nullptr) {
            if constexpr (std::is_same_v<Ty, cfg::Excludes>) {
              self->excludes(other);
            } else {
              self->needs(other);
            }
          }
        }
      };
      (applyOne(cfgItems), ...);
    },
    spec.cfg
  );
}

// Both phases over a tuple of specs, in registration order.
template<typename... Specs>
void registerAll(
  CLI::App* app,
  std::tuple<Specs...> const& specs,
  std::vector<configstore::KeyDef>& entries
) {
  [&]<std::size_t... I>(std::index_sequence<I...>) {
    auto const options = std::array<CLI::Option*, sizeof...(Specs)>{
      registerOne(app, std::get<I>(specs), entries)...
    };
    (applyDeps(app, options[I], std::get<I>(specs)), ...);
  }(std::index_sequence_for<Specs...>{});
}
