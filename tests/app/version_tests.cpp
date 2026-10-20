#include "app/app_entry.h"
#include "app/version.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE(
  "the version flag reports the version token and no build time",
  "[appentry][version]"
) {
  int exitCode = -1;
  auto const out = testutils::captureStdout([&] {
    char arg0[] = "encro";
    char arg1[] = "--version";
    char* argv[] = {arg0, arg1, nullptr};
    exitCode = appentry::run(2, argv);
  });

  REQUIRE(exitCode == 0);

  // ENCRO_VERSION and ENCRO_GIT_COMMIT come from the `encroVersion` declaration
  // in xmake.lua and the build's git metadata; the exact line pins both, so a
  // build-time clause cannot creep back in.
  CHECK(
    out
    == std::string{"encro "}
      + appentry::versionToken(ENCRO_VERSION, ENCRO_GIT_COMMIT)
      + "\n"
  );
}
