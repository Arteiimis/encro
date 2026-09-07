// Compile-time verification that the platform's standard library hardening is
// active in the translation units of the test suite itself: a build-config
// regression that silently drops the hardening define fails the suite here
// instead of passing unnoticed. Runtime behavior (violation -> crash record)
// is covered by the crash runtime integration tests.
#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <version>  // pulls in the STL's own configuration macros (_MSVC_STL_VERSION et al.)

TEST_CASE("stdlib hardening is active at compile time", "[hardening]") {
#if defined(_MSVC_STL_VERSION)
  STATIC_REQUIRE(_MSVC_STL_HARDENING == 1);
#else
  STATIC_REQUIRE(_GLIBCXX_ASSERTIONS == 1);
#endif
}
