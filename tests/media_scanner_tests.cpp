#include "core/media_scanner.h"
#include "test_utils.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace std::literals;
using testutils::writeTextFile;

TEST_CASE("scanByExtensions matches single file", "[media-scanner]") {
  TempDir temp;
  auto const filePath = temp.path / "sample.mp4";
  writeTextFile(filePath);

  auto const scanRes = media::scanByExtensions(filePath, std::array{".mp4"sv}, false);
  REQUIRE(scanRes);
  auto const& results = scanRes->matches;

  REQUIRE(results.size() == 1);
  CHECK(results.front() == filePath);
}

TEST_CASE("scanByExtensions ignores non-matching file", "[media-scanner]") {
  TempDir temp;
  auto const filePath = temp.path / "sample.mov";
  writeTextFile(filePath);

  auto const scanRes = media::scanByExtensions(filePath, std::array{".mp4"sv}, false);
  REQUIRE(scanRes);
  CHECK(scanRes->matches.empty());
}

TEST_CASE("scanByExtensions respects non-recursive", "[media-scanner]") {
  TempDir temp;
  auto const topFile = temp.path / "a.mp4";
  auto const nestedDir = temp.path / "nested";
  auto const nestedFile = nestedDir / "b.mp4";
  fs::create_directories(nestedDir);
  writeTextFile(topFile);
  writeTextFile(nestedFile);

  auto const scanRes = media::scanByExtensions(temp.path, std::array{".mp4"sv}, false);
  REQUIRE(scanRes);
  auto const& results = scanRes->matches;

  REQUIRE(results.size() == 1);
  CHECK(results.front() == topFile);
}

TEST_CASE("scanByExtensions includes recursive matches", "[media-scanner]") {
  TempDir temp;
  auto const topFile = temp.path / "a.mp4";
  auto const nestedDir = temp.path / "nested";
  auto const nestedFile = nestedDir / "b.mp4";
  fs::create_directories(nestedDir);
  writeTextFile(topFile);
  writeTextFile(nestedFile);

  auto const scanRes = media::scanByExtensions(temp.path, std::array{".mp4"sv}, true);
  REQUIRE(scanRes);
  auto const& results = scanRes->matches;

  REQUIRE(results.size() == 2);
  CHECK(std::ranges::find(results, topFile) != results.end());
  CHECK(std::ranges::find(results, nestedFile) != results.end());
}

TEST_CASE("scanByExtensions reports an unreadable root as an error", "[media-scanner]") {
  TempDir temp;
  auto const missing = temp.path / "does_not_exist";

  auto const scanRes = media::scanByExtensions(missing, std::array{".mp4"sv}, true);

  // An unreadable root must never look like an empty scan.
  REQUIRE_FALSE(scanRes);
  CHECK(scanRes.error().find("not a readable directory") != std::string::npos);
  CHECK(scanRes.error().find(missing.string()) != std::string::npos);
}

TEST_CASE(
  "scanByExtensions returns empty success for a readable-but-empty directory",
  "[media-scanner]"
) {
  TempDir temp;

  auto const scanRes = media::scanByExtensions(temp.path, std::array{".mp4"sv}, true);

  REQUIRE(scanRes);
  CHECK(scanRes->matches.empty());
}

TEST_CASE(
  "scanByExtensions skips dot-prefixed directories recursively",
  "[media-scanner]"
) {
  TempDir temp;
  auto const topFile = temp.path / "a.mp4";
  auto const hiddenDir = temp.path / ".encro";
  auto const hiddenFile = hiddenDir / "segments" / "x.mp4";
  auto const legacyDir = temp.path / ".compress_tmp_q90";
  auto const legacyFile = legacyDir / "y.mp4";
  auto const nestedDir = temp.path / "nested";
  auto const nestedFile = nestedDir / "b.mp4";
  fs::create_directories(hiddenDir / "segments");
  fs::create_directories(legacyDir);
  fs::create_directories(nestedDir);
  writeTextFile(topFile);
  writeTextFile(hiddenFile);
  writeTextFile(legacyFile);
  writeTextFile(nestedFile);

  auto const scanRes = media::scanByExtensions(temp.path, std::array{".mp4"sv}, true);
  REQUIRE(scanRes);
  auto const& results = scanRes->matches;

  REQUIRE(results.size() == 2);
  CHECK(std::ranges::find(results, topFile) != results.end());
  CHECK(std::ranges::find(results, nestedFile) != results.end());
  CHECK(std::ranges::find(results, hiddenFile) == results.end());
  CHECK(std::ranges::find(results, legacyFile) == results.end());
  CHECK(scanRes->warnings.empty());
}

TEST_CASE(
  "scanByExtensions skips dot-prefixed files in non-recursive scan",
  "[media-scanner]"
) {
  TempDir temp;
  auto const regular = temp.path / "a.mp4";
  auto const hidden = temp.path / ".hidden.mp4";
  writeTextFile(regular);
  writeTextFile(hidden);

  auto const scanRes = media::scanByExtensions(temp.path, std::array{".mp4"sv}, false);
  REQUIRE(scanRes);
  auto const& results = scanRes->matches;

  REQUIRE(results.size() == 1);
  CHECK(results.front() == regular);
}
