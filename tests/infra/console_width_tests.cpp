#include "infra/console_width.h"
#include "infra/env.h"

#include <catch2/catch_all.hpp>

#include <string>

namespace {

class ScopedEnvVar {
public:
  ScopedEnvVar(std::string name, std::string value)
    : name_(std::move(name)), hadOriginal_(false) {
    if (auto const current = processenv::readEnvVar(name_); current.has_value()) {
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
  void set(std::string const& value) {
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s(name_.c_str(), value.c_str());
#else
    setenv(name_.c_str(), value.c_str(), 1);
#endif
  }

  void unset() {
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
