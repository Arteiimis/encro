#include "infra/stacktrace.h"

#include <catch2/catch_all.hpp>

#include <string>
#include <vector>

TEST_CASE("formatStacktrace handles empty frames", "[stacktrace]") {
  auto const formatted = crash::formatStacktrace(std::vector<std::string>{});
  CHECK(formatted == "<empty stacktrace>");
}

TEST_CASE("captureStacktrace returns printable output", "[stacktrace]") {
  auto const frames = crash::captureStacktrace();
  auto const formatted = crash::formatStacktrace(frames);

  CHECK_FALSE(formatted.empty());
}

TEST_CASE("formatStacktrace prefixes frame indexes", "[stacktrace]") {
  auto const frames = std::vector<std::string>{"frameA", "frameB"};
  auto const formatted = crash::formatStacktrace(frames);

  CHECK(formatted.find("#00 frameA") != std::string::npos);
  CHECK(formatted.find("#01 frameB") != std::string::npos);
}
