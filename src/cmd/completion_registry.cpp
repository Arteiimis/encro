#include "cmd/completion_registry.h"

namespace completion {

auto optionValues() -> std::map<std::string, ValueInfo>& {
  static std::map<std::string, ValueInfo> registry;
  return registry;
}

auto configKeyOptions() -> std::map<std::string, std::string>& {
  static std::map<std::string, std::string> registry;
  return registry;
}

void recordCandidates(std::string longName, std::vector<std::string> values) {
  optionValues()[std::move(longName)].candidates = std::move(values);
}

void recordNumeric(std::string longName) {
  optionValues()[std::move(longName)].numeric = true;
}

void recordConfigKey(std::string_view key, std::string longName) {
  configKeyOptions().insert_or_assign(std::string{key}, std::move(longName));
}

auto valueInfoOf(std::string const& longName) -> ValueInfo const* {
  auto const& registry = optionValues();
  auto const found = registry.find(longName);
  return found == registry.end() ? nullptr : &found->second;
}

auto configKeys() -> std::vector<std::string> {
  auto names = std::vector<std::string>{};
  names.reserve(configKeyOptions().size());
  for (auto const& entry: configKeyOptions()) { names.push_back(entry.first); }
  return names;
}

auto longNameOfConfigKey(std::string const& key) -> std::string const* {
  auto const& registry = configKeyOptions();
  auto const found = registry.find(key);
  return found == registry.end() ? nullptr : &found->second;
}

}  // namespace completion
