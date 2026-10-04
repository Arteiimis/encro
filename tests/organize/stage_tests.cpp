// Ported pipeline stages: cache, naming sanitizer, execute, report
// (change add-local-character-grouping tasks 1.2-1.4).
#include "organize/cache.h"
#include "organize/disposition.h"
#include "organize/execute.h"
#include "organize/naming.h"
#include "organize/report.h"
#include "core/display_text.h"
#include "core/sha256.h"
#include "infra/stop_signal.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <format>
#include <ranges>
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
    .tags =
      {.general = {tag("pink_hair", 0.9), tag("2girls", 0.8)},
       .character = {tag("hatsune_miku", 0.7), tag("kagamine_rin", 0.6)},
       .rating = {tag("general", 0.95)}},
    // Decimal fractions that do not survive a lossy number format.
    .identity = {0.1f, 0.2f, 0.3f},
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
  REQUIRE(restored->identity.size() == 3);
  CHECK(restored->identity[0] == 0.1f);
  CHECK(restored->identity[2] == 0.3f);

  auto const empty = reloaded.get("hash-b");
  REQUIRE(empty.has_value());  // analyzed-but-empty is distinct from absent
  CHECK(empty->tags.general.empty());
  CHECK(empty->tags.character.empty());

  CHECK(!reloaded.get("hash-c").has_value());
}

TEST_CASE("AnalysisCache discards a cache whose format version differs", "[organize]") {
  // The stored analysis changes meaning when a model or its input preparation
  // changes, so the version is part of the meaning: a file from any other
  // version — the old entry shape, or the file the previous release wrote —
  // reads as empty, and the run re-analyzes instead of clustering on features
  // computed a different way.
  auto temp = TempDir{};
  auto const cachePath = temp.path / "organized" / ".cache" / "analysis.json";
  fs::create_directories(cachePath.parent_path());

  for (
    auto const& body: {
      R"({"version":1,"images":{"hash-a":{"general":[["pink_hair",0.9]],"character":[],"rating":[]}}})",
      R"({"version":2,"images":{"hash-a":{"general":[["pink_hair",0.9]],"character":[],"rating":[],"identity":[0.5,0.5]}}})",
    }
  ) {
    testutils::writeTextFile(cachePath, body);
    auto cache = organize::AnalysisCache{cachePath};
    cache.load();
    CHECK(!cache.get("hash-a").has_value());
  }
}

TEST_CASE("AnalysisCache stores only what routing can read, per category", "[organize]") {
  // Floors track the consuming thresholds (design D6): general tags are read at
  // kNamingConfidenceFloor (0.55) and up for `unknown_` names, character routing
  // reads kWeakConfidence (0.53) and up. Storing lower confidences let the
  // tagger's ~0.5 identity noise (all ~2.7k vocabulary characters per image)
  // dominate the store without ever influencing a decision. The identity
  // feature is stored whole: it is the clustering input, not a tag.
  auto temp = TempDir{};
  auto cache = organize::AnalysisCache{temp.path / "analysis.json"};
  cache.load();
  cache.put(
    "hash-a",
    organize::AnalysisResult{
      .tags =
        {.general =
           {tag("pink_hair", 0.9),
            tag("vector_noise", 0.5499),
            tag("background_detail", 0.05)},
         .character =
           {tag("hatsune_miku", 0.7),
            tag("weak_agreement", 0.53),
            tag("identity_noise", 0.52)},
         .rating = {tag("general", 0.95), tag("sub_floor", 0.09)}},
      .identity = {0.5f, 0.5f},
    }
  );

  auto const restored = cache.get("hash-a");
  REQUIRE(restored.has_value());
  REQUIRE(restored->tags.general.size() == 1);
  CHECK(restored->tags.general.front().tag == "pink_hair");
  REQUIRE(restored->tags.character.size() == 2);
  CHECK(restored->tags.character[0].tag == "hatsune_miku");
  CHECK(restored->tags.character[1].tag == "weak_agreement");
  REQUIRE(restored->tags.rating.size() == 1);
  CHECK(restored->tags.rating.front().tag == "general");
  CHECK(restored->identity == std::vector<float>{0.5f, 0.5f});
}

TEST_CASE("AnalysisCache buffers puts until the flush interval", "[organize]") {
  // One atomic rewrite per analyzed image made cache I/O quadratic in
  // collection size; the pipeline batches puts and flushes at boundaries.
  auto temp = TempDir{};
  auto const cachePath = temp.path / "organized" / ".cache" / "analysis.json";
  auto cache = organize::AnalysisCache{cachePath, /*flushEveryPuts=*/2};
  cache.load();

  cache.put("hash-a", organize::AnalysisResult{});
  CHECK(!fs::exists(cachePath));                    // buffered: no rewrite per image
  cache.put("hash-b", organize::AnalysisResult{});
  CHECK(fs::exists(cachePath));                     // interval reached: batch persisted

  cache.put("hash-c", organize::AnalysisResult{});  // buffered tail
  auto midRun = organize::AnalysisCache{cachePath};
  midRun.load();
  CHECK(midRun.get("hash-a").has_value());
  CHECK(!midRun.get("hash-c").has_value());

  cache.flush();
  auto reloaded = organize::AnalysisCache{cachePath};
  reloaded.load();
  CHECK(reloaded.get("hash-a").has_value());
  CHECK(reloaded.get("hash-b").has_value());
  CHECK(reloaded.get("hash-c").has_value());
}

TEST_CASE("AnalysisCache flush without pending puts writes nothing", "[organize]") {
  auto temp = TempDir{};
  auto const cachePath = temp.path / "analysis.json";
  auto cache = organize::AnalysisCache{cachePath, /*flushEveryPuts=*/8};
  cache.load();
  cache.flush();
  CHECK(!fs::exists(cachePath));
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
     .contentHash = core::sha256Hex("content-a"),
     .folderName = "hatsune_miku",
     .folderSource = organize::FolderSource::CharacterTag},
    {.path = temp.path / "b.png",
     .contentHash = core::sha256Hex("content-b"),
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

// The copy loop's checkpoint runs before each image's work. Its position
// inside the loop is not observable from this seam (a copy is atomic: staging
// file + rename), so this pins the contract a stop must satisfy — once it is
// requested, no further image is copied — while the pipeline case pins the
// same contract through runOrganize.
TEST_CASE(
  "executeOrganize copies nothing once a stop is requested",
  "[organize][stop-signal]"
) {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "a.png", "content-a");
  testutils::writeTextFile(temp.path / "b.png", "content-b");

  auto const items = std::vector<organize::ImageItem>{
    {.path = temp.path / "a.png",
     .contentHash = core::sha256Hex("content-a"),
     .folderName = "hatsune_miku"},
    {.path = temp.path / "b.png",
     .contentHash = core::sha256Hex("content-b"),
     .folderName = "hatsune_miku"},
  };

  auto const stopGuard = testutils::ScopedStopSignalReset{};
  stopsignal::requestStop();

  auto const stats = organize::executeOrganize(temp.path, items, false);
  CHECK(stats.canceled);
  CHECK(stats.copied == 0);
  CHECK(stats.errors.empty());
  CHECK_FALSE(fs::exists(temp.path / "organized" / "hatsune_miku" / "a.png"));
  CHECK_FALSE(fs::exists(temp.path / "organized" / "hatsune_miku" / "b.png"));
}

TEST_CASE(
  "executeOrganize skips identical and suffixes different content",
  "[organize]"
) {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "a.png", "content-a");
  auto const hashA = core::sha256Hex("content-a");
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
  items.front().contentHash = core::sha256Hex("different-content");
  auto third = organize::executeOrganize(temp.path, items, false);
  CHECK(third.copied == 1);
  CHECK(fs::exists(temp.path / "organized" / "miku" / "a_2.png"));
}

TEST_CASE("executeOrganize dry run copies nothing", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "a.png", "content-a");
  auto items = std::vector<organize::ImageItem>{
    {.path = temp.path / "a.png",
     .contentHash = core::sha256Hex("content-a"),
     .folderName = "miku"},
  };

  auto const stats = organize::executeOrganize(temp.path, items, true);
  CHECK(stats.copied == 0);
  CHECK(!fs::exists(temp.path / "organized" / "miku"));
}

// ── Incremental report surface (change organize-existing-structure task 6.1):
// disposition summary, zero-match rows, alignment ──

TEST_CASE("renderReport lists dispositions with the demotion hint", "[organize]") {
  auto report = organize::ReportData{
    .dispositions = {
      {"角色A", organize::DispositionKind::Reference, false},
      {"mix", organize::DispositionKind::Input, false},
      {".stash", organize::DispositionKind::Ignored, false},
      {"杂物", organize::DispositionKind::Reference, true},
    },
  };

  auto const text = organize::renderReport(report, 80);
  CHECK(text.find("角色A: reference") != std::string::npos);
  CHECK(text.find("mix: input") != std::string::npos);
  CHECK(text.find(".stash: ignored") != std::string::npos);
  CHECK(text.find("杂物: demoted") != std::string::npos);
  CHECK(text.find("--ingest 杂物") != std::string::npos);

  // Outside incremental runs the section is absent entirely.
  auto plain = organize::ReportData{
    .folders = {organize::FolderReportLine{
      .folder = "miku",
      .images = 1,
      .source = organize::FolderSource::CharacterTag,
    }}
  };
  CHECK(organize::renderReport(plain, 80).find("dispositions:") == std::string::npos);
}

TEST_CASE("renderReport adds zero-count rows for unmatched references", "[organize]") {
  auto report = organize::ReportData{
    .folders = {organize::FolderReportLine{
      .folder = "角色A",
      .images = 5,
      .source = organize::FolderSource::FolderMatch,
    }},
    .dispositions = {
      {"角色A", organize::DispositionKind::Reference, false},
      {"角色B", organize::DispositionKind::Reference, false},
      // Demoted folders are named in the summary, not as zero rows.
      {"杂物", organize::DispositionKind::Reference, true},
      {"mix", organize::DispositionKind::Input, false},
    },
  };

  auto const text = organize::renderReport(report, 80);
  CHECK(text.find("角色B") != std::string::npos);
  // The zero row keeps the table's columns: the count sits at the same
  // offset as every counted row (folder width 30 + one space).
  auto zeroRow = std::string{};
  for (auto const& line: text | std::views::split('\n')) {
    auto const row = std::string{line.begin(), line.end()};
    if (row.find("角色B") != std::string::npos && row != "  角色B: reference") {
      zeroRow = row;
      break;
    }
  }
  REQUIRE_FALSE(zeroRow.empty());
  // The count sits at display column 31 like every counted row; its position
  // in bytes depends on the name's width, so locate it and measure the prefix.
  auto const countPos = zeroRow.find("     0");
  REQUIRE(countPos != std::string::npos);
  CHECK(displaytext::displayWidth(zeroRow.substr(0, countPos)) == 31);
  // No zero row for the folder the table already counts, none for demoted or
  // input folders; 杂物 shows on one summary line (its hint names it again).
  CHECK(testutils::countOccurrences(text, "角色A") == 2);  // table + summary
  auto miscLines = std::size_t{0};
  for (auto const& line: text | std::views::split('\n')) {
    auto const row = std::string{line.begin(), line.end()};
    if (row.find("杂物") != std::string::npos) { ++miscLines; }
  }
  CHECK(miscLines == 1);                                 // the summary line only
  CHECK(testutils::countOccurrences(text, "mix") == 1);  // summary only
}

TEST_CASE("zero-match rows keep the table aligned for long names", "[organize]") {
  auto const longName = std::string{"unknown_animal_ears_eyepatch_black_hair"};
  auto report = organize::ReportData{
    .dispositions = {{longName, organize::DispositionKind::Reference, false}},
  };

  auto const text = organize::renderReport(report, 50);
  auto lines = std::vector<std::string>{};
  for (auto const& line: text | std::views::split('\n')) {
    lines.emplace_back(line.begin(), line.end());
  }
  REQUIRE(lines.size() >= 3);  // header, rule, zero-match row
  CHECK(text.find("...") != std::string::npos);
  // The count sits in the same 6-wide column as the header's images label.
  CHECK(lines[2].substr(31, 6) == "     0");
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
  // Map order sorts folder names: miku < mixed < unknown_pink.
  CHECK(folders[0].folder == "miku");
  CHECK(folders[0].images == 2);
  CHECK(folders[0].source == organize::FolderSource::CharacterTag);
  CHECK(folders[1].folder == "mixed");
  CHECK(folders[1].source == organize::FolderSource::Mixed);
  CHECK(folders[2].folder == "unknown_pink");
  CHECK(folders[2].source == organize::FolderSource::NewCluster);
}
