#include "infra/console_width.h"

#include <catch2/catch_all.hpp>

#include <cstdlib>
#include <string>

namespace {

auto readEnvVar(std::string const& name) -> std::optional<std::string> {
#if defined(_WIN32) || defined(_WIN64)
  auto* value = static_cast<char*>(nullptr);
  auto len = std::size_t{0};
  if (_dupenv_s(&value, &len, name.c_str()) != 0 || value == nullptr) {
    return std::nullopt;
  }

  auto result = std::optional<std::string>{};
  if (len > 1) { result = std::string{value}; }
  std::free(value);
  return result;
#else
  if (auto const* value = std::getenv(name.c_str()); value != nullptr) {
    return std::string{value};
  }
  return std::nullopt;
#endif
}

class ScopedEnvVar {
public:
  ScopedEnvVar(std::string name, std::string value)
    : name_(std::move(name)), hadOriginal_(false) {
    if (auto const current = readEnvVar(name_); current.has_value()) {
      originalValue_ = current.value();
      hadOriginal_ = true;
    }
    set(value);
  }

  ScopedEnvVar(ScopedEnvVar const&) = delete;
  auto operator=(ScopedEnvVar const&) -> ScopedEnvVar& = delete;

  ~ScopedEnvVar() {
    if (hadOriginal_) {
      set(originalValue_);
    } else {
      unset();
    }
  }

private:
  auto set(std::string const& value) -> void {
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s(name_.c_str(), value.c_str());
#else
    setenv(name_.c_str(), value.c_str(), 1);
#endif
  }

  auto unset() -> void {
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s(name_.c_str(), "");
#else
    unsetenv(name_.c_str());
#endif
  }

  std::string name_;
  std::string originalValue_;
  bool hadOriginal_;
};

}  // namespace

TEST_CASE("resolveColumns caps detected width", "[console-width]") {
  auto const columnsVar = ScopedEnvVar{"COLUMNS", "200"};

  auto const width = consolewidth::resolveColumns({
    .defaultColumns = 80,
    .maxColumns = 120,
  });

  CHECK(width == 120);
}

TEST_CASE("resolveColumns honors minimum width floor", "[console-width]") {
  auto const columnsVar = ScopedEnvVar{"COLUMNS", "24"};

  auto const width = consolewidth::resolveColumns({
    .defaultColumns = 80,
    .minColumns = 40,
    .maxColumns = 120,
  });

  CHECK(width == 40);
}
