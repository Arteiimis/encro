#include "app/app_entry.h"

#include "infra/stop_signal.h"

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

TEST_CASE("run status maps the exit code and the stop fact", "[appentry]") {
  CHECK(appentry::runStatus(0, false) == "success");
  // A successful run stays successful even if a stop arrives in the last instant.
  CHECK(appentry::runStatus(0, true) == "success");
  CHECK(appentry::runStatus(stopsignal::kCanceledExitCode, false) == "interrupted");
  CHECK(appentry::runStatus(1, false) == "failed");
  // Preview reports its cancel as an error (exit 1), so a pending stop is what
  // distinguishes it from a plain failure.
  CHECK(appentry::runStatus(1, true) == "interrupted");
}
