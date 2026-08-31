#include "app/app_entry.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <string_view>

TEST_CASE("help intro line includes description and build timestamp", "[appentry]") {
  auto const line = appentry::helpIntroLine();
  constexpr auto prefix =
    std::string_view{"encro: Universal video encoder/converter/packer | build: "};

  REQUIRE(line.starts_with(prefix));

  auto const timestamp = line.substr(prefix.size());
  REQUIRE(timestamp.size() == 19);
  CHECK(timestamp[4] == '-');
  CHECK(timestamp[7] == '-');
  CHECK(timestamp[10] == ' ');
  CHECK(timestamp[13] == ':');
  CHECK(timestamp[16] == ':');
}
