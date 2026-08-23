// Declarative CLI11 option registration: an option is a single OptSpec
// (name, bound field, description) plus a variadic list of cfg tokens that
// configure the option. Registration happens in two phases — phase 1 binds
// and configures every option, phase 2 resolves Excludes/Needs by pointer
// (option names like "-p,--pack" are not resolvable via get_option).
#pragma once

#include <CLI/CLI.hpp>

#include <array>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace cfg {

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
  void operator()(CLI::Option* option) const { option->check(CLI::Range(lo, hi)); }
};

struct PositiveNumber {
  void operator()(CLI::Option* option) const { option->check(CLI::PositiveNumber); }
};

struct NonNegativeNumber {
  void operator()(CLI::Option* option) const { option->check(CLI::NonNegativeNumber); }
};

struct Required {
  void operator()(CLI::Option* option) const { option->required(); }
};

struct Members {
  std::vector<std::string> legal;
  // non-aggregate on purpose: lets the legal values be written as a flat
  // initializer list {"a", "b"} instead of nested vector braces
  Members(std::initializer_list<std::string> values): legal(values) { }
  void operator()(CLI::Option* option) const { option->check(CLI::IsMember(legal)); }
};

struct CheckedTransformer {
  std::vector<std::pair<std::string, std::string>> mapping;
  // non-aggregate on purpose: lets the mapping be written as a flat pair list
  // {{a, b}, {c, d}} like upstream CheckedTransformer
  CheckedTransformer(std::initializer_list<std::pair<std::string, std::string>> values)
    : mapping(values) { }
  void operator()(CLI::Option* option) const {
    option->transform(CLI::CheckedTransformer(mapping));
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

// Phase 1: register and configure one option. bool bindings are flags,
// everything else takes a value.
template<typename Spec>
auto registerOne(CLI::App* app, Spec const& spec) -> CLI::Option* {
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
  return option;
}

// Phase 2: resolve Excludes/Needs by long name against the same app scope.
template<typename Spec>
auto applyDeps(CLI::App* app, CLI::Option* self, Spec const& spec) -> void {
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
auto registerAll(CLI::App* app, std::tuple<Specs...> const& specs) -> void {
  [&]<std::size_t... I>(std::index_sequence<I...>) {
    auto const options =
      std::array<CLI::Option*, sizeof...(Specs)>{registerOne(app, std::get<I>(specs))...};
    (applyDeps(app, options[I], std::get<I>(specs)), ...);
  }(std::index_sequence_for<Specs...>{});
}
