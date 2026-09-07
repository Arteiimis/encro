// Compile-time verification that the platform's standard library hardening is
// active in the translation units of the test suite itself. A build-config
// regression that silently drops the hardening define fails here (via #error
// at preprocessing) instead of leaving the suite green with checks disabled.
// Runtime behavior (violation -> crash record) is covered by the crash
// runtime integration tests.
#include <version>  // pulls in the STL's own configuration macros (_MSVC_STL_VERSION et al.)

#if defined(_MSVC_STL_VERSION)
  #if !defined(_MSVC_STL_HARDENING) || _MSVC_STL_HARDENING != 1
    #error "MSVC STL hardening is not enabled; check the Windows defines in xmake.lua"
  #endif
#else
  #if !defined(_GLIBCXX_ASSERTIONS)
    #error \
      "libstdc++ precondition checks are not enabled; check the non-Windows defines in xmake.lua"
  #endif
#endif

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

TEST_CASE("stdlib hardening is active at compile time", "[hardening]") {
#if defined(_MSVC_STL_VERSION)
  STATIC_REQUIRE(_MSVC_STL_HARDENING == 1);
#else
  STATIC_REQUIRE(_GLIBCXX_ASSERTIONS == 1);
#endif
}
