#include "infra/console_width.h"
#include "infra/env.h"

#include "test_utils.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <string>

namespace {

}  // namespace

TEST_CASE("resolveColumns caps detected width", "[console-width]") {
  auto const columnsVar = testutils::ScopedEnvVar{"COLUMNS", "200"};

  auto const width = consolewidth::resolveColumns({
    .defaultColumns = 80,
    .maxColumns = 120,
  });

  CHECK(width == 120);
}

TEST_CASE("resolveColumns honors minimum width floor", "[console-width]") {
  auto const columnsVar = testutils::ScopedEnvVar{"COLUMNS", "24"};

  auto const width = consolewidth::resolveColumns({
    .defaultColumns = 80,
    .minColumns = 40,
    .maxColumns = 120,
  });

  CHECK(width == 40);
}
