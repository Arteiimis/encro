#include "core/media_scanner.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <array>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace std::literals;

namespace {

void touchFile(fs::path const& filePath) {
  std::ofstream out{filePath};
  REQUIRE(out.is_open());
  out << "x";
}

}  // namespace

TEST_CASE("scanByExtensions matches single file", "[media-scanner]") {
  TempDir temp;
  auto const filePath = temp.path / "sample.mp4";
  touchFile(filePath);

  auto const results = media::scanByExtensions(filePath, std::array{".mp4"sv}, false);

  REQUIRE(results.size() == 1);
  CHECK(results.front() == filePath);
}

TEST_CASE("scanByExtensions ignores non-matching file", "[media-scanner]") {
  TempDir temp;
  auto const filePath = temp.path / "sample.mov";
  touchFile(filePath);

  auto const results = media::scanByExtensions(filePath, std::array{".mp4"sv}, false);

  CHECK(results.empty());
}

TEST_CASE("scanByExtensions respects non-recursive", "[media-scanner]") {
  TempDir temp;
  auto const topFile = temp.path / "a.mp4";
  auto const nestedDir = temp.path / "nested";
  auto const nestedFile = nestedDir / "b.mp4";
  fs::create_directories(nestedDir);
  touchFile(topFile);
  touchFile(nestedFile);

  auto const results = media::scanByExtensions(temp.path, std::array{".mp4"sv}, false);

  REQUIRE(results.size() == 1);
  CHECK(results.front() == topFile);
}

TEST_CASE("scanByExtensions includes recursive matches", "[media-scanner]") {
  TempDir temp;
  auto const topFile = temp.path / "a.mp4";
  auto const nestedDir = temp.path / "nested";
  auto const nestedFile = nestedDir / "b.mp4";
  fs::create_directories(nestedDir);
  touchFile(topFile);
  touchFile(nestedFile);

  auto const results = media::scanByExtensions(temp.path, std::array{".mp4"sv}, true);

  REQUIRE(results.size() == 2);
  CHECK(std::ranges::find(results, topFile) != results.end());
  CHECK(std::ranges::find(results, nestedFile) != results.end());
}
