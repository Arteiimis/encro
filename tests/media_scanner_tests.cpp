#include "core/path_utf8.h"
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
  CHECK(scanRes.error().find(pathutf8::fromPath(missing)) != std::string::npos);
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

TEST_CASE(
  "scanByExtensions matches uppercase and mixed-case extensions in a directory",
  "[media-scanner]"
) {
  TempDir temp;
  auto const upper = temp.path / "CLIP.MP4";
  auto const mixed = temp.path / "PHOTO.Jpg";
  writeTextFile(upper);
  writeTextFile(mixed);

  auto const scanRes =
    media::scanByExtensions(temp.path, std::array{".mp4"sv, ".jpg"sv}, false);
  REQUIRE(scanRes);
  auto const& results = scanRes->matches;

  REQUIRE(results.size() == 2);
  CHECK(std::ranges::contains(results, upper));
  CHECK(std::ranges::contains(results, mixed));
}

TEST_CASE(
  "scanByExtensions matches an uppercase extension for a single-file root",
  "[media-scanner]"
) {
  TempDir temp;
  auto const filePath = temp.path / "PHOTO.Jpg";
  writeTextFile(filePath);

  auto const scanRes = media::scanByExtensions(filePath, std::array{".jpg"sv}, false);

  REQUIRE(scanRes);
  REQUIRE(scanRes->matches.size() == 1);
  CHECK(scanRes->matches.front() == filePath);
}

TEST_CASE("listFiles reports a missing root as an error", "[media-scanner]") {
  TempDir temp;
  // Not a directory: an error, never a silently empty listing.
  auto const missing = media::listFiles(temp.path / "nope", true);
  REQUIRE_FALSE(missing);
  CHECK(missing.error().find("not a readable directory") != std::string::npos);

  auto const file = testutils::writeTextFile(temp.path / "a.txt");
  CHECK_FALSE(media::listFiles(file, true));
}

TEST_CASE(
  "listFiles keeps hidden files but can skip work directories",
  "[media-scanner]"
) {
  TempDir temp;
  auto const root = temp.path / "root";
  fs::create_directories(root / ".encro" / "segments");
  fs::create_directories(root / "sub");
  testutils::writeTextFile(root / ".gitignore", "x");
  testutils::writeTextFile(root / ".encro" / "job-state.json", "x");
  testutils::writeTextFile(root / ".encro" / "segments" / "seg_0.ts", "x");
  testutils::writeTextFile(root / "sub" / "clip.mp4", "x");
  testutils::writeTextFile(root / "sub" / ".hidden.txt", "x");

  // Packing a user tree keeps its hidden files...
  auto const plain = media::listFiles(root, true);
  REQUIRE(plain);
  CHECK(plain->matches.size() == 5);

  // ...but a work directory named by the caller is never descended into.
  constexpr auto kSkipped = std::array<std::string_view, 1>{".encro"};
  auto const filtered = media::listFiles(root, true, kSkipped);
  REQUIRE(filtered);
  CHECK(filtered->matches.size() == 3);
  CHECK(std::ranges::none_of(filtered->matches, [](fs::path const& path) {
    return pathutf8::fromPath(path).find(".encro") != std::string::npos;
  }));
}

#if !defined(_WIN32)
TEST_CASE("listFiles skips an unreadable entry and keeps walking", "[media-scanner]") {
  TempDir temp;
  auto const root = temp.path / "root";
  auto const locked = root / "locked";
  fs::create_directories(locked);
  testutils::writeTextFile(root / "a.txt", "x");
  testutils::writeTextFile(locked / "b.txt", "x");

  fs::permissions(locked, fs::perms::none);
  auto const listed = media::listFiles(root, true);
  fs::permissions(locked, fs::perms::owner_all);

  // The unreadable entry is left out, the walk never throws, and what could be
  // read is still returned: the permission-denied option suppresses the error
  // instead of reporting it, so no warning is promised for this case.
  REQUIRE(listed);
  CHECK(std::ranges::any_of(listed->matches, [](fs::path const& path) {
    return path.filename() == "a.txt";
  }));
}
#endif

TEST_CASE("scanByExtensions does not fold letters outside A-Z", "[media-scanner]") {
  TempDir temp;
  // A `ch | 0x20` fold would turn '[' (0x5B) into '{' (0x7B) and match.
  auto const filePath = temp.path / "a.{";
  writeTextFile(filePath);

  auto const scanRes = media::scanByExtensions(filePath, std::array{".["sv}, false);

  REQUIRE(scanRes);
  CHECK(scanRes->matches.empty());
}
