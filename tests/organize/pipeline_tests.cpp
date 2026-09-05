// Pipeline integration over the FakeTagger seam (tasks 4.1-4.4): staging,
// mixed/uncategorized semantics, dry-run, recluster, resume, rename teaching.
#include "organize/pipeline.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <filesystem>
#include <map>
#include <string>

namespace fs = std::filesystem;

namespace {

auto tag(std::string name, double confidence) -> organize::TagScore {
  return organize::TagScore{.tag = std::move(name), .confidence = confidence};
}

// Scripted engine: filename -> output; unscripted files fail (decode error).
class FakeTagger final: public tagger::TaggerEngine {
public:
  std::map<std::string, tagger::TaggerOutput> byName;
  std::atomic<int> calls{0};

  auto classify(fs::path const& path) -> eh::Result<tagger::TaggerOutput> override {
    ++calls;
    auto const it = byName.find(path.filename().string());
    if (it == byName.end()) { return eh::makeError("no fixture for {}", path.string()); }
    return it->second;
  }
};

auto makeOptions(fs::path const& root) -> organize::Options {
  return organize::Options{
    .root = root,
    .recursive = false,
    .minConfidence = 0.35,
    .modelDir = root / "models",
    .maxJobs = 1,
  };
}

}  // namespace

TEST_CASE("pipeline files known characters, clusters, and mixed", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku-1.png", "miku-1");
  testutils::writeTextFile(temp.path / "miku-2.png", "miku-2");
  testutils::writeTextFile(temp.path / "oc-a.png", "oc-a");
  testutils::writeTextFile(temp.path / "duo.png", "duo");

  auto engine = FakeTagger{};
  engine.byName["miku-1.png"] = {.character = {tag("hatsune_miku", 0.9)}};
  engine.byName["miku-2.png"] = {.character = {tag("hatsune_miku", 0.9)}};
  engine.byName["oc-a.png"] = {.general = {tag("pink_hair", 0.9), tag("blue_eyes", 0.8)}};
  engine.byName["duo.png"] =
    {.general = {tag("2girls", 0.9)}, .character = {tag("a", 0.8), tag("b", 0.7)}};

  auto const report = organize::runOrganize(makeOptions(temp.path), engine, nullptr);
  REQUIRE(report.has_value());
  CHECK(report->scanned == 4);
  CHECK(report->copied == 4);

  // Confident tag folder.
  CHECK(fs::exists(temp.path / "organized" / "hatsune_miku" / "miku-1.png"));
  CHECK(fs::exists(temp.path / "organized" / "hatsune_miku" / "miku-2.png"));
  // Multi-subject with two confident tags.
  CHECK(fs::exists(temp.path / "organized" / "mixed" / "duo.png"));
  // Single-subject remainder clustered into an unknown_ folder.
  auto foundUnknown = false;
  for (auto const& entry: fs::directory_iterator{temp.path / "organized"}) {
    if (entry.path().filename().string().starts_with("unknown_")) {
      foundUnknown = fs::exists(entry.path() / "oc-a.png");
    }
  }
  CHECK(foundUnknown);
  // Originals untouched.
  CHECK(testutils::readTextFile(temp.path / "miku-1.png") == "miku-1");
}

TEST_CASE("pipeline routes count-tag multi-subject and analysis failures", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "duo.png", "duo");
  testutils::writeTextFile(temp.path / "broken.png", "broken");

  auto engine = FakeTagger{};
  engine.byName["duo.png"] = {.general = {tag("2girls", 0.9)}};  // no character tag

  auto const report = organize::runOrganize(makeOptions(temp.path), engine, nullptr);
  REQUIRE(report.has_value());
  CHECK(fs::exists(temp.path / "organized" / "mixed" / "duo.png"));
  // Unscripted file fails analysis -> uncategorized, run continues.
  CHECK(fs::exists(temp.path / "organized" / "uncategorized" / "broken.png"));
}

TEST_CASE("dry run reports but copies nothing", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku.png", "miku");

  auto engine = FakeTagger{};
  engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};

  auto options = makeOptions(temp.path);
  options.dryRun = true;
  auto const report = organize::runOrganize(options, engine, nullptr);
  REQUIRE(report.has_value());
  CHECK(report->copied == 0);
  CHECK(report->folders.size() == 1);
  CHECK(!fs::exists(temp.path / "organized" / "hatsune_miku"));
}

TEST_CASE("re-run resumes from cache without re-classifying", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku.png", "miku");

  auto engine = FakeTagger{};
  engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};

  (void)organize::runOrganize(makeOptions(temp.path), engine, nullptr);
  REQUIRE(engine.calls.load() == 1);

  // Second run: cache hits, no new classify calls, no duplicate copies.
  auto const second = organize::runOrganize(makeOptions(temp.path), engine, nullptr);
  REQUIRE(second.has_value());
  CHECK(engine.calls.load() == 1);
  CHECK(second->cacheHits == 1);
  CHECK(second->copied == 0);
  CHECK(second->skippedExisting == 1);
}

TEST_CASE("recluster discards cached analysis and re-classifies", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku.png", "miku");

  auto engine = FakeTagger{};
  engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};

  (void)organize::runOrganize(makeOptions(temp.path), engine, nullptr);
  REQUIRE(engine.calls.load() == 1);

  auto options = makeOptions(temp.path);
  options.recluster = true;
  (void)organize::runOrganize(options, engine, nullptr);
  CHECK(engine.calls.load() == 2);
}

TEST_CASE("renamed character folder teaches subsequent runs", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku.png", "miku");

  auto engine = FakeTagger{};
  engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};

  (void)organize::runOrganize(makeOptions(temp.path), engine, nullptr);
  CHECK(fs::exists(temp.path / "organized" / "hatsune_miku" / "miku.png"));

  // User renames the folder; the next run files by the new name and never
  // recreates the old one.
  fs::rename(
    temp.path / "organized" / "hatsune_miku",
    temp.path / "organized" / "初音ミク"
  );
  testutils::writeTextFile(temp.path / "miku2.png", "miku2");
  engine.byName["miku2.png"] = {.character = {tag("hatsune_miku", 0.9)}};

  auto const report = organize::runOrganize(makeOptions(temp.path), engine, nullptr);
  REQUIRE(report.has_value());
  CHECK(fs::exists(temp.path / "organized" / "初音ミク" / "miku.png"));
  CHECK(fs::exists(temp.path / "organized" / "初音ミク" / "miku2.png"));
  CHECK(!fs::exists(temp.path / "organized" / "hatsune_miku"));
}

TEST_CASE("renderReport lists folders with sources and totals", "[organize]") {
  auto report = organize::ReportData{
    .folders = {organize::FolderReportLine{
      .folder = "hatsune_miku",
      .images = 2,
      .source = organize::FolderSource::CharacterTag
    }},
    .scanned = 2,
    .copied = 2,
    .skippedExisting = 0,
    .cacheHits = 0,
  };
  auto const text = organize::renderReport(report);
  CHECK(text.find("hatsune_miku") != std::string::npos);
  CHECK(text.find("character tag") != std::string::npos);
  CHECK(text.find("scanned 2 images") != std::string::npos);
}
