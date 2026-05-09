#include "app/app_entry.h"

#include <catch2/catch_all.hpp>

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

TEST_CASE("help intro line version format", "[appentry]") {
  // --version output format: "encro v1.6 (build: YYYY-MM-DD HH:MM:SS)"
  // helpIntroLine() uses compileTimestamp() which shares the timestamp format.
  // Verify timestamp is well-formed (YYYY-MM-DD HH:MM:SS, 19 chars).
  auto const line = appentry::helpIntroLine();

  // Extract trailing timestamp: helpIntroLine ends with "build: YYYY-MM-DD HH:MM:SS"
  auto const buildPos = line.rfind("build: ");
  REQUIRE(buildPos != std::string::npos);
  auto const timestamp = line.substr(buildPos + 7);  // 7 = strlen("build: ")
  REQUIRE(timestamp.size() == 19);
  CHECK(timestamp[4] == '-');
  CHECK(timestamp[7] == '-');
  CHECK(timestamp[10] == ' ');
  CHECK(timestamp[13] == ':');
  CHECK(timestamp[16] == ':');
}
