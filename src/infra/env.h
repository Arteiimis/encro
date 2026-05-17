#pragma once

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace processenv {

inline auto readEnvVar(std::string_view name) -> std::optional<std::string> {
#if defined(_WIN32) || defined(_WIN64)
  auto value = std::unique_ptr<char>{};
  auto size = std::size_t{0};
  auto const key = std::string{name};
  if (_dupenv_s(std::out_ptr(value), &size, key.c_str()) != 0) { return std::nullopt; }
  if (value == nullptr) { return std::nullopt; }

  auto result = std::string{value.get()};

  return result;
#else
  auto const key = std::string{name};
  auto const* value = std::getenv(key.c_str());
  if (value == nullptr) { return std::nullopt; }
  return std::string{value};
#endif
}

inline auto readNonEmptyEnvVar(std::string_view name) -> std::optional<std::string> {
  auto value = readEnvVar(name);
  if (!value.has_value() || value->empty()) { return std::nullopt; }
  return value;
}

}  // namespace processenv
