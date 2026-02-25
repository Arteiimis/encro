#include "test_utils.h"
#include "utils/utils.h"

#include <catch2/catch_all.hpp>

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("findFFmpeg returns empty for invalid install dir", "[toolchain]") {
  TempDir temp;
  auto const emptyDir = temp.path / "empty";
  fs::create_directories(emptyDir);

  auto const result = findFFmpeg(emptyDir);
  CHECK_FALSE(result.has_value());
}

TEST_CASE("findFFprobe returns empty for invalid install dir", "[toolchain]") {
  TempDir temp;
  auto const emptyDir = temp.path / "empty";
  fs::create_directories(emptyDir);

  auto const result = findFFprobe(emptyDir);
  CHECK_FALSE(result.has_value());
}
