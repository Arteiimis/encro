#include "app/app_entry.h"
#include "app/version.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("--version reports the declared version and no other", "[appentry][version]") {
  int exitCode = -1;
  auto const out = testutils::captureStdout([&] {
    char arg0[] = "encro";
    char arg1[] = "--version";
    char* argv[] = {arg0, arg1, nullptr};
    exitCode = appentry::run(2, argv);
  });

  REQUIRE(exitCode == 0);

  // ENCRO_VERSION comes from the `encroVersion` declaration in xmake.lua, so
  // this fails whenever the reported string drifts from it.
  auto const expected = std::string{"encro "} + std::string{ENCRO_VERSION};
  CHECK(out.find(expected) != std::string::npos);
  CHECK(out.find("encro v") == std::string::npos);
}
