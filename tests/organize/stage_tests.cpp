// Ported pipeline stages: cache, naming sanitizer, execute, report
// (change add-local-character-grouping tasks 1.2-1.4).
#include "organize/cache.h"
#include "organize/execute.h"
#include "organize/naming.h"
#include "organize/report.h"
#include "organize/sha256.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <set>
#include <string>

namespace fs = std::filesystem;

namespace {

organize::TagScore tag(std::string name, double confidence) {
  return organize::TagScore{.tag = std::move(name), .confidence = confidence};
}

}  // namespace

TEST_CASE("sanitizeCharacterName yields lowercase snake names", "[organize]") {
  CHECK(organize::sanitizeCharacterName("Hatsune Miku") == "hatsune_miku");
  CHECK(organize::sanitizeCharacterName("Furina") == "furina");
  CHECK(organize::sanitizeCharacterName("  Re:Zero! Emilia ") == "re_zero_emilia");
  CHECK(organize::sanitizeCharacterName("初音ミク") == "");
  CHECK(organize::sanitizeCharacterName("__--__") == "");
  CHECK(organize::sanitizeCharacterName("a") == "a");
}

TEST_CASE("sanitizeCharacterName caps length", "[organize]") {
  CHECK(organize::sanitizeCharacterName(std::string(80, 'x')).size() == 48);
}

TEST_CASE("fallbackCharacterName is deterministic and sanitized", "[organize]") {
  auto const first = organize::fallbackCharacterName("hash-a|hash-b");
  auto const second = organize::fallbackCharacterName("hash-a|hash-b");
  CHECK(first == second);
  CHECK(first.starts_with("char_"));
  CHECK(first.size() == 13);
  CHECK(organize::fallbackCharacterName("other") != first);
}

TEST_CASE("assignUniqueFolderName suffixes distinct clusters", "[organize]") {
  auto used = std::set<std::string>{};
  auto const first = organize::assignUniqueFolderName("miku", used);
  CHECK(first.name == "miku");
  CHECK(!first.renamed);
  auto const second = organize::assignUniqueFolderName("miku", used);
  CHECK(second.name == "miku_2");
  CHECK(second.renamed);
  auto const third = organize::assignUniqueFolderName("miku", used);
  CHECK(third.name == "miku_3");
  CHECK(organize::assignUniqueFolderName("furi", used).name == "furi");
}

TEST_CASE("AnalysisCache roundtrips raw analysis by content hash", "[organize]") {
  auto temp = TempDir{};
  auto const cachePath = temp.path / "organized" / ".cache" / "analysis.json";
  auto const result = organize::AnalysisResult{
    .general = {tag("pink_hair", 0.9), tag("2girls", 0.8)},
    .character = {tag("hatsune_miku", 0.7), tag("kagamine_rin", 0.6)},
    .rating = {tag("general", 0.95)},
  };

  {
    auto cache = organize::AnalysisCache{cachePath};
    cache.load();
    cache.put("hash-a", result);
    cache.put("hash-b", organize::AnalysisResult{});
  }

  auto reloaded = organize::AnalysisCache{cachePath};
  reloaded.load();

  auto const restored = reloaded.get("hash-a");
  REQUIRE(restored.has_value());
  CHECK(restored == result);

  auto const empty = reloaded.get("hash-b");
  REQUIRE(empty.has_value());  // analyzed-but-empty is distinct from absent
  CHECK(empty->general.empty());
  CHECK(empty->character.empty());

  CHECK(!reloaded.get("hash-c").has_value());
}

TEST_CASE("AnalysisCache drops sub-floor confidences", "[organize]") {
  auto temp = TempDir{};
  auto cache = organize::AnalysisCache{temp.path / "analysis.json"};
  cache.load();
  cache.put(
    "hash-a",
    organize::AnalysisResult{
      .general = {tag("pink_hair", 0.9), tag("background_detail", 0.05)},
      .character = {tag("hatsune_miku", 0.09)},
      .rating = {},
    }
  );

  auto const restored = cache.get("hash-a");
  REQUIRE(restored.has_value());
  REQUIRE(restored->general.size() == 1);
  CHECK(restored->general.front().tag == "pink_hair");
  CHECK(restored->character.empty());
}

TEST_CASE("AnalysisCache treats a corrupt or wrong-version file as empty", "[organize]") {
  auto temp = TempDir{};
  auto const cachePath = temp.path / "analysis.json";
  testutils::writeTextFile(cachePath, "{not json");

  auto cache = organize::AnalysisCache{cachePath};
  cache.load();
  CHECK(!cache.get("anything").has_value());

  cache.put("h", organize::AnalysisResult{});
  auto reloaded = organize::AnalysisCache{cachePath};
  reloaded.load();
  REQUIRE(reloaded.get("h").has_value());
}

TEST_CASE("AnalysisCache clear discards the file", "[organize]") {
  auto temp = TempDir{};
  auto const cachePath = temp.path / "analysis.json";
  auto cache = organize::AnalysisCache{cachePath};
  cache.load();
  cache.put("hash-a", organize::AnalysisResult{});

  cache.clear();
  auto reloaded = organize::AnalysisCache{cachePath};
  reloaded.load();
  CHECK(!reloaded.get("hash-a").has_value());
}

TEST_CASE("executeOrganize copies originals untouched into one folder", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "a.png", "content-a");
  testutils::writeTextFile(temp.path / "b.png", "content-b");

  auto items = std::vector<organize::ImageItem>{
    {.path = temp.path / "a.png",
     .contentHash = organize::sha256Hex("content-a"),
     .folderName = "hatsune_miku",
     .folderSource = organize::FolderSource::CharacterTag},
    {.path = temp.path / "b.png",
     .contentHash = organize::sha256Hex("content-b"),
     .folderName = "hatsune_miku"},
  };

  auto const stats = organize::executeOrganize(temp.path, items, false);
  CHECK(stats.copied == 2);
  CHECK(stats.errors.empty());
  CHECK(fs::exists(temp.path / "organized" / "hatsune_miku" / "a.png"));
  CHECK(fs::exists(temp.path / "organized" / "hatsune_miku" / "b.png"));
  // Originals untouched.
  CHECK(testutils::readTextFile(temp.path / "a.png") == "content-a");
}

TEST_CASE(
  "executeOrganize skips identical and suffixes different content",
  "[organize]"
) {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "a.png", "content-a");
  auto const hashA = organize::sha256Hex("content-a");
  auto items = std::vector<organize::ImageItem>{
    {.path = temp.path / "a.png", .contentHash = hashA, .folderName = "miku"},
  };

  (void)organize::executeOrganize(temp.path, items, false);
  auto second = organize::executeOrganize(temp.path, items, false);
  CHECK(second.copied == 0);
  CHECK(second.skippedExisting == 1);
  CHECK(!fs::exists(temp.path / "organized" / "miku" / "a_2.png"));

  // Same name, different content -> numeric suffix.
  testutils::writeTextFile(temp.path / "a.png", "different-content");
  items.front().contentHash = organize::sha256Hex("different-content");
  auto third = organize::executeOrganize(temp.path, items, false);
  CHECK(third.copied == 1);
  CHECK(fs::exists(temp.path / "organized" / "miku" / "a_2.png"));
}

TEST_CASE("executeOrganize dry run copies nothing", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "a.png", "content-a");
  auto items = std::vector<organize::ImageItem>{
    {.path = temp.path / "a.png",
     .contentHash = organize::sha256Hex("content-a"),
     .folderName = "miku"},
  };

  auto const stats = organize::executeOrganize(temp.path, items, true);
  CHECK(stats.copied == 0);
  CHECK(!fs::exists(temp.path / "organized" / "miku"));
}

TEST_CASE("buildFoldersSection aggregates counts and sources", "[organize]") {
  auto items = std::vector<organize::ImageItem>{
    {.path = "1",
     .folderName = "miku",
     .folderSource = organize::FolderSource::CharacterTag},
    {.path = "2",
     .folderName = "miku",
     .folderSource = organize::FolderSource::CharacterTag},
    {.path = "3",
     .folderName = "unknown_pink",
     .folderSource = organize::FolderSource::NewCluster},
    {.path = "4", .folderName = "mixed", .folderSource = organize::FolderSource::Mixed},
  };

  auto const folders = organize::buildFoldersSection(items);
  REQUIRE(folders.size() == 3);
  auto const& miku = folders[0];
  CHECK(miku.folder == "miku");
  CHECK(miku.images == 2);
  CHECK(miku.source == organize::FolderSource::CharacterTag);
}
