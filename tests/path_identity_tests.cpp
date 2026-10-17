#include "core/collision_naming.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace fs = std::filesystem;
using collisionnaming::pathIdentityString;
using collisionnaming::shortPathHash;
using collisionnaming::stablePathLess;
using collisionnaming::stablePathString;

namespace {

auto const kUpper = fs::path{"dir/./A.mp4"};
auto const kLower = fs::path{"dir/a.mp4"};

}  // namespace

TEST_CASE("path identity folds case only where the filesystem folds", "[path-identity]") {
#if defined(_WIN32)
  // A case-insensitive filesystem: both spellings are one file, one identity.
  CHECK(pathIdentityString(kUpper) == pathIdentityString(kLower));
#else
  // A case-sensitive filesystem: they are two files and must stay two identities.
  CHECK(pathIdentityString(kUpper) != pathIdentityString(kLower));
#endif
  CHECK(pathIdentityString("dir/./sub/../A.mp4") != pathIdentityString("dir/B.mp4"));
}

TEST_CASE("path ordering folds case on every platform", "[path-identity]") {
  CHECK(stablePathString(kUpper) == stablePathString(kLower));
  CHECK_FALSE(stablePathLess(kUpper, kLower));
  CHECK_FALSE(stablePathLess(kLower, kUpper));
}

TEST_CASE("short path hash follows path identity", "[path-identity]") {
#if defined(_WIN32)
  CHECK(shortPathHash(kUpper) == shortPathHash(kLower));
#else
  CHECK(shortPathHash(kUpper) != shortPathHash(kLower));
#endif
}
