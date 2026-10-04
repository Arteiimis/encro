// Pipeline integration over the FakeTagger seam (tasks 4.1-4.4): staging,
// mixed/uncategorized semantics, dry-run, recluster, resume, rename teaching.
#include "core/display_text.h"
#include "core/progress.h"
#include "core/sha256.h"

#include "organize/cache.h"
#include "organize/cluster.h"
#include "organize/execute.h"
#include "organize/pipeline.h"
#include "organize/teach.h"

#include "cmd/cmd.h"
#include "infra/stop_signal.h"
#include "organize/organize_command.h"
#include "tagger/mapping.h"
#include "utils/utils.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <filesystem>
#include <format>
#include <map>
#include <ranges>
#include <string>
#include <vector>

#if defined(_WIN32)
  #include <windows.h>
#endif

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
  std::vector<std::string> classifiedPaths;

  auto classify(fs::path const& path) -> eh::Result<tagger::TaggerOutput> override {
    ++calls;
    classifiedPaths.push_back(path.filename().string());
    auto const it = byName.find(path.filename().string());
    if (it == byName.end()) { return eh::makeError("no fixture for {}", path.string()); }
    return it->second;
  }
};

// Scripted identity engine: filename -> feature. An unscripted file has no
// identity evidence, so it can still be routed by character tag or count tag
// but never clusters; a scripted one is normalized the way the real engine's
// contract says.
class FakeFeatureEngine final: public tagger::FeatureEngine {
public:
  std::map<std::string, std::vector<float>> byName;
  std::atomic<int> calls{0};

  auto extract(fs::path const& path) -> eh::Result<std::vector<float>> override {
    ++calls;
    auto const it = byName.find(path.filename().string());
    if (it == byName.end()) { return eh::makeError("no fixture for {}", path.string()); }
    auto feature = it->second;
    tagger::normalizeFeature(feature);
    return feature;
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

// Runs one image, holding `cosine` against a taught folder whose single member
// carries `unitFeature(1.0)`, and returns the folder the image landed in. The
// referenced member is never rescanned (it sits under organized/), so its
// feature has to come from the cache.
auto taughtFolderFor(double cosine) -> std::string {
  auto temp = TempDir{};
  auto const bytes = std::string{"member-bytes"};
  testutils::writeTextFile(temp.path / "organized" / "taught" / "member.png", bytes);
  testutils::writeTextFile(temp.path / "new.png", "new-bytes");

  auto cache =
    organize::AnalysisCache{temp.path / "organized" / ".cache" / "analysis.json"};
  cache.load();
  cache.put(
    core::sha256Hex(bytes),
    organize::AnalysisResult{.identity = testutils::unitFeature(1.0)}
  );

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  features.byName["new.png"] = testutils::unitFeature(cosine);
  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());

  for (auto const& entry: fs::directory_iterator{temp.path / "organized"}) {
    if (!entry.is_directory()) { continue; }
    if (fs::exists(entry.path() / "new.png")) { return entry.path().filename().string(); }
  }
  return {};
}

#if defined(_WIN32)
// A file every other check still sees as a regular file, but that no reader
// can open: a share mode of 0 refuses all further opens until this handle
// closes. Windows-only, because the portable way to make a file unreadable is
// to drop its read permission and the current process is subject to that too.
class ExclusivelyLockedFile {
public:
  explicit ExclusivelyLockedFile(fs::path const& path)
    : handle_(
        ::CreateFileW(
          path.c_str(),
          GENERIC_READ,
          0,
          nullptr,
          OPEN_EXISTING,
          FILE_ATTRIBUTE_NORMAL,
          nullptr
        )
      ) { }

  ~ExclusivelyLockedFile() {
    if (handle_ != INVALID_HANDLE_VALUE) { ::CloseHandle(handle_); }
  }

  ExclusivelyLockedFile(ExclusivelyLockedFile const&) = delete;
  auto operator=(ExclusivelyLockedFile const&) -> ExclusivelyLockedFile& = delete;

  bool isLocked() const { return handle_ != INVALID_HANDLE_VALUE; }

private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};
#endif

// True when the run left a cluster uncaptured: it named an unknown_ folder.
bool hasUnknownFolder(organize::ReportData const& report) {
  for (auto const& line: report.folders) {
    if (line.folder.starts_with(organize::kUnknownPrefix)) { return true; }
  }
  return false;
}

// How many images the report files under one folder name.
std::size_t folderImages(organize::ReportData const& report, std::string_view name) {
  for (auto const& line: report.folders) {
    if (line.folder == name) { return line.images; }
  }
  return 0;
}

}  // namespace

// The first run is the case this change exists for: the folder the routing
// creates in this very run becomes a reference, so a cluster joins it now
// instead of waiting for a second run (design D1/D2/D5).
TEST_CASE(
  "a first run captures a cluster into the folder it just created",
  "[organize]"
) {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku.png", "miku");
  testutils::writeTextFile(temp.path / "solo.png", "solo");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};
  engine.byName["solo.png"] = {.general = {tag("pink_hair", 0.9)}};
  features.byName["miku.png"] = testutils::unitFeature(1.0);
  // Cosine 0.9 against the folder's only member: above the feature-only
  // default, so the cluster matches the reference the routing just made.
  features.byName["solo.png"] = testutils::unitFeature(0.9);

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());
  CHECK(fs::exists(temp.path / "organized" / "hatsune_miku" / "miku.png"));
  CHECK(fs::exists(temp.path / "organized" / "hatsune_miku" / "solo.png"));
  CHECK_FALSE(hasUnknownFolder(*report));
  CHECK(folderImages(*report, "hatsune_miku") == 2);
}

// A folder the user renamed owns its character tag; the run's own destination
// carries that name, so the two are one reference and nothing is duplicated.
TEST_CASE(
  "a renamed folder keeps owning the character a first run would add",
  "[organize]"
) {
  auto temp = TempDir{};
  auto const renamed = temp.path / "organized" / "初音ミク";
  fs::create_directories(renamed);
  auto const seat = std::string{"old-bytes"};
  testutils::writeTextFile(renamed / "old.png", seat);
  {
    auto cache =
      organize::AnalysisCache{temp.path / "organized" / ".cache" / "analysis.json"};
    cache.put(
      core::sha256Hex(seat),
      organize::AnalysisResult{
        .tags = {.character = {tag("hatsune_miku", 0.9)}},
        .identity = testutils::unitFeature(1.0),
      }
    );
  }

  testutils::writeTextFile(temp.path / "miku.png", "miku");
  testutils::writeTextFile(temp.path / "solo.png", "solo");
  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};
  engine.byName["solo.png"] = {.general = {tag("pink_hair", 0.9)}};
  features.byName["miku.png"] = testutils::unitFeature(1.0);
  features.byName["solo.png"] = testutils::unitFeature(0.9);

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());
  CHECK(fs::exists(renamed / "miku.png"));
  CHECK(fs::exists(renamed / "solo.png"));
  CHECK_FALSE(fs::exists(temp.path / "organized" / "hatsune_miku"));
}

// A run that files nothing by character tag has no references of its own, so
// its clusters keep the names they had before this change.
TEST_CASE(
  "a run with no character routing produces no references of its own",
  "[organize]"
) {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "a.png", "a");
  testutils::writeTextFile(temp.path / "b.png", "b");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["a.png"] = {.general = {tag("pink_hair", 0.9)}};
  engine.byName["b.png"] = {.general = {tag("pink_hair", 0.9)}};
  features.byName["a.png"] = testutils::unitFeature(1.0);
  features.byName["b.png"] = testutils::unitFeature(1.0);

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());
  // Same result as before the requirement existed: one cluster, named from the
  // tags its members carry, and nothing captured.
  CHECK(report->folders.size() == 1);
  CHECK(folderImages(*report, "unknown_pink_hair") == 2);
}

// The threshold still decides: a cluster that matches no reference keeps its
// unknown_ name even though a reference now exists.
TEST_CASE("a cluster below the threshold keeps its unknown name", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku.png", "miku");
  testutils::writeTextFile(temp.path / "far.png", "far");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};
  engine.byName["far.png"] = {.general = {tag("blue_eyes", 0.9)}};
  features.byName["miku.png"] = testutils::unitFeature(1.0);
  features.byName["far.png"] = testutils::unitFeature(0.2);

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());
  CHECK(fs::exists(temp.path / "organized" / "hatsune_miku" / "miku.png"));
  CHECK(folderImages(*report, "hatsune_miku") == 1);
  CHECK(hasUnknownFolder(*report));
}

TEST_CASE("pipeline files known characters, clusters, and mixed", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku-1.png", "miku-1");
  testutils::writeTextFile(temp.path / "miku-2.png", "miku-2");
  testutils::writeTextFile(temp.path / "oc-a.png", "oc-a");
  testutils::writeTextFile(temp.path / "duo.png", "duo");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["miku-1.png"] = {.character = {tag("hatsune_miku", 0.9)}};
  engine.byName["miku-2.png"] = {.character = {tag("hatsune_miku", 0.9)}};
  engine.byName["oc-a.png"] = {.general = {tag("pink_hair", 0.9), tag("blue_eyes", 0.8)}};
  engine.byName["duo.png"] =
    {.general = {tag("2girls", 0.9)}, .character = {tag("a", 0.8), tag("b", 0.7)}};
  // Only the clustered remainder needs a feature; the tagged and
  // multi-subject images are routed before clustering reads one.
  features.byName["oc-a.png"] = testutils::unitFeature(1.0);

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
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

TEST_CASE("lone weak character candidate claims the image", "[organize]") {
  // Second subject with no facial detail -> weak/no identity signal; the
  // main character still owns the image (user acceptance scenario).
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "venti-solo.png", "vs");
  testutils::writeTextFile(temp.path / "venti-duo.png", "vd");
  testutils::writeTextFile(temp.path / "clash.png", "clash");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["venti-solo.png"] =
    {.general = {tag("2boys", 0.7)}, .character = {tag("venti", 0.55)}};
  engine.byName["venti-duo.png"] =
    {.general = {tag("2boys", 0.7)}, .character = {tag("venti", 0.55)}};
  engine.byName["clash.png"] = {.character = {tag("venti", 0.55), tag("kieran", 0.52)}};

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());
  // Weak-but-only candidate files by character even though count tags fired.
  CHECK(fs::exists(temp.path / "organized" / "venti" / "venti-solo.png"));
  CHECK(fs::exists(temp.path / "organized" / "venti" / "venti-duo.png"));
  // The stronger weak candidate wins the image; no mixed for weak-only.
  CHECK(fs::exists(temp.path / "organized" / "venti" / "clash.png"));
}

TEST_CASE("pipeline routes count-tag multi-subject and analysis failures", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "duo.png", "duo");
  testutils::writeTextFile(temp.path / "broken.png", "broken");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["duo.png"] = {.general = {tag("2girls", 0.9)}};  // no character tag

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());
  CHECK(fs::exists(temp.path / "organized" / "mixed" / "duo.png"));
  // Unscripted file fails analysis -> uncategorized, run continues.
  CHECK(fs::exists(temp.path / "organized" / "uncategorized" / "broken.png"));
}

TEST_CASE("dry run reports but copies nothing", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku.png", "miku");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};

  auto options = makeOptions(temp.path);
  options.dryRun = true;
  auto const report = organize::runOrganize(options, engine, features, nullptr);
  REQUIRE(report.has_value());
  CHECK(report->copied == 0);
  CHECK(report->folders.size() == 1);
  CHECK(!fs::exists(temp.path / "organized" / "hatsune_miku"));
}

TEST_CASE("re-run resumes from cache without re-classifying", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku.png", "miku");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};

  // NOLINTNEXTLINE(bugprone-unused-return-value): effects asserted below
  (void)organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(engine.calls.load() == 1);

  // Second run: cache hits, no new inference of either product, no duplicate
  // copies.
  auto const second =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(second.has_value());
  CHECK(engine.calls.load() == 1);
  CHECK(features.calls.load() == 1);
  CHECK(second->cacheHits == 1);
  CHECK(second->copied == 0);
  CHECK(second->skippedExisting == 1);
}

TEST_CASE("a cache from an older format version is re-analyzed", "[organize]") {
  // The run-level half of the cache's version rule: an entry written by a
  // release that prepared the model input differently must not be read, so its
  // destination is not the one the stale analysis would have chosen.
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku.png", "miku");

  auto const cachePath = temp.path / "organized" / ".cache" / "analysis.json";
  fs::create_directories(cachePath.parent_path());
  auto const hash = core::sha256File(temp.path / "miku.png");
  auto const entry =
    R"({"general":[],"character":[["stale_character",0.9]],"rating":[],"identity":[1.0,0.0]}}})";
  auto const writeCache = [&](std::string const& version) {
    testutils::writeTextFile(
      cachePath,
      std::string{"{\"version\":"} + version + ",\"images\":{\"" + hash + "\":" + entry
    );
  };

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};

  // The version the previous release wrote: it must not be trusted, so the
  // image is classified again and lands where the fresh analysis sends it.
  writeCache("2");
  auto const stale =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(stale.has_value());
  CHECK(engine.calls.load() == 1);
  CHECK(stale->cacheHits == 0);
  CHECK(fs::exists(temp.path / "organized" / "hatsune_miku"));
  CHECK(!fs::exists(temp.path / "organized" / "stale_character"));

  // The other direction, which keeps the case from passing on a fixture the
  // cache never parses at all: the same entry under the current version is
  // trusted, so the cached analysis decides the destination without inference.
  fs::remove_all(temp.path / "organized" / "hatsune_miku");
  writeCache("3");
  {
    // The fixture itself has to parse: without this the case could pass on a
    // file the cache never reads, which is how the first version of it misled.
    auto probe = organize::AnalysisCache{cachePath};
    probe.load();
    REQUIRE(probe.get(hash).has_value());
  }
  auto const current =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(current.has_value());
  CHECK(current->cacheHits == 1);
  CHECK(engine.calls.load() == 1);
  CHECK(fs::exists(temp.path / "organized" / "stale_character"));
}

TEST_CASE("teaching weighs the tag side like clustering does", "[organize]") {
  // Two halves, both red under a feature-only comparison at 0.643: agreeing
  // identity tags lift a pair whose features alone would miss the default
  // (0.8*0.63 + 0.2*1.0 = 0.704 clears 0.70), and a reference with no
  // identity-tag evidence is judged on the feature alone against 0.74, so
  // 0.72 no longer reaches it.
  auto const run = [](double cosine, std::vector<organize::TagScore> memberTags) {
    auto temp = TempDir{};
    auto const referenceDir = temp.path / "organized" / "taught";
    fs::create_directories(referenceDir);
    auto const memberBytes = std::string{"member"};
    testutils::writeTextFile(referenceDir / "member.png", memberBytes);

    auto cache =
      organize::AnalysisCache{temp.path / "organized" / ".cache" / "analysis.json"};
    cache.put(
      core::sha256Hex(memberBytes),
      organize::AnalysisResult{
        .tags = {.general = std::move(memberTags), .character = {}, .rating = {}},
        .identity = testutils::unitFeature(1.0),
      }
    );

    testutils::writeTextFile(temp.path / "new.png", "new");
    auto engine = FakeTagger{};
    auto features = FakeFeatureEngine{};
    engine.byName["new.png"] = {.general = {tag("pink_hair", 0.9)}};
    features.byName["new.png"] = testutils::unitFeature(cosine);

    auto const report =
      organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
    REQUIRE(report.has_value());
    for (auto const& folder: report->folders) {
      if (folder.folder == "taught") { return true; }
    }
    return false;
  };

  // Agreeing tags carry the pair over the combined default.
  CHECK(run(0.63, {tag("pink_hair", 0.9)}));
  // No tag evidence on the reference side: the feature alone, below 0.74.
  CHECK_FALSE(run(0.72, {}));
}

TEST_CASE("a run above the clustering ceiling falls back and reports it", "[organize]") {
  auto temp = TempDir{};
  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  for (auto const index: {0, 1, 2}) {
    auto const name = std::format("oc{}.png", index);
    testutils::writeTextFile(temp.path / name, name);
    engine.byName[name] = {};
    features.byName[name] = testutils::unitFeature(1.0);
  }

  auto options = makeOptions(temp.path);
  options.clusterImageCeiling = 2;  // three unassigned images: above it
  // The fallback line is a warning, so it goes to stderr: capture that stream
  // and keep every assertion outside the redirection window.
  auto const capturePath = temp.path / "stderr.log";
  auto report = std::optional<organize::ReportData>{};
  {
    auto const capture = testutils::StderrCapture{capturePath};
    auto const result = organize::runOrganize(options, engine, features, nullptr);
    if (result.has_value()) { report = *result; }
  }
  auto const captured = testutils::readTextFile(capturePath);

  CHECK(captured.find("falling back") != std::string::npos);
  // Every image still lands in exactly one folder: three copies, one folder.
  REQUIRE(report.has_value());
  auto copies = std::size_t{0};
  for (auto const& entry: fs::recursive_directory_iterator(temp.path / "organized")) {
    if (entry.is_regular_file() && entry.path().parent_path().filename() != ".cache") {
      ++copies;
    }
  }
  CHECK(copies == 3);
  CHECK(report->folders.size() == 1);
}

TEST_CASE("identical content is analyzed once and its twin is a skip", "[organize]") {
  // Two files with the same bytes share one content hash: the stage classifies
  // that content once, the twin is filtered as already analyzed (skipped, not
  // attempted), and the bar's total is the uncached remainder rather than
  // everything the scan found.
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku-a.png", "miku-bytes");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["miku-a.png"] = {.character = {tag("hatsune_miku", 0.9)}};

  auto const first =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(first.has_value());
  REQUIRE(engine.calls.load() == 1);

  // The same bytes under a second name (one shared content hash) plus a new
  // file: the cached analysis covers both miku files, so only the new content
  // is left to classify. miku-b.png has no fixture on purpose -- classifying
  // it would fail, not pass silently.
  testutils::writeTextFile(temp.path / "miku-b.png", "miku-bytes");
  testutils::writeTextFile(temp.path / "rin.png", "rin-bytes");
  engine.byName["rin.png"] = {.character = {tag("kagamine_rin", 0.9)}};

  auto progressCtx = progress::ProgressContext{};
  auto const second =
    organize::runOrganize(makeOptions(temp.path), engine, features, &progressCtx);
  REQUIRE(second.has_value());
  CHECK(engine.calls.load() == 2);
  CHECK(features.calls.load() == 2);
  CHECK(second->cacheHits == 2);

  // One bar, one completion: total 1 is the uncached remainder (3 scanned, 2
  // skipped), the count today's analysisTasks.size() produced.
  REQUIRE(progressCtx.barCount() == 1);
  auto const barText = progressCtx.postfixText(0);
  CHECK(barText.starts_with("1/1 - "));
  CHECK(barText.ends_with(" img/s"));
}

TEST_CASE("recluster discards cached analysis and re-classifies", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku.png", "miku");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};

  // NOLINTNEXTLINE(bugprone-unused-return-value): effects asserted below
  (void)organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(engine.calls.load() == 1);

  auto options = makeOptions(temp.path);
  options.recluster = true;
  // NOLINTNEXTLINE(bugprone-unused-return-value): effects asserted below
  (void)organize::runOrganize(options, engine, features, nullptr);
  CHECK(engine.calls.load() == 2);
}

TEST_CASE("renamed character folder teaches subsequent runs", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku.png", "miku");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};

  // NOLINTNEXTLINE(bugprone-unused-return-value): effects asserted below
  (void)organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  CHECK(fs::exists(temp.path / "organized" / "hatsune_miku" / "miku.png"));

  // User renames the folder; the next run files by the new name and never
  // recreates the old one.
  fs::rename(
    temp.path / "organized" / "hatsune_miku",
    temp.path / "organized" / "初音ミク"
  );
  testutils::writeTextFile(temp.path / "miku2.png", "miku2");
  engine.byName["miku2.png"] = {.character = {tag("hatsune_miku", 0.9)}};

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());
  CHECK(fs::exists(temp.path / "organized" / "初音ミク" / "miku.png"));
  CHECK(fs::exists(temp.path / "organized" / "初音ミク" / "miku2.png"));
  CHECK(!fs::exists(temp.path / "organized" / "hatsune_miku"));
}

TEST_CASE("teaching captures a cluster at the threshold", "[organize]") {
  // A cluster joins a reference folder when its centroid matches the folder's
  // mean feature at the identity threshold (spec "Teaching by renamed
  // folders").
  CHECK(taughtFolderFor(0.9) == "taught");
}

TEST_CASE("a cluster below the threshold keeps its unknown_ name", "[organize]") {
  CHECK(taughtFolderFor(0.5).starts_with("unknown_"));
}

#if defined(_WIN32)
TEST_CASE("teaching skips a member it cannot read", "[organize]") {
  // sha256File signals an unreadable file with "", and "" is a reachable
  // cache key: the analysis stage stores under whatever scan computed, which
  // is "" for a file that was already unreadable then (AnalysisCache::put has
  // no empty-key guard). Teaching must not look that key up, or an unrelated
  // analysis joins the folder reference.
  auto temp = TempDir{};
  auto const member = testutils::writeTextFile(
    temp.path / "organized" / "hatsune_miku" / "locked.png",
    "locked"
  );
  fs::create_directories(temp.path / "organized" / ".cache");

  auto cache =
    organize::AnalysisCache{temp.path / "organized" / ".cache" / "analysis.json"};
  cache
    .put("", organize::AnalysisResult{.tags = {.character = {tag("hatsune_miku", 0.9)}}});

  auto const lock = ExclusivelyLockedFile{member};
  REQUIRE(lock.isLocked());
  // The premise the case rests on: the member is still enumerable and still a
  // regular file, but reading it now yields no digest. Without this the case
  // would pass vacuously whenever the lock failed to block the read.
  REQUIRE(core::sha256File(member).empty());

  auto const references = organize::buildFolderReferences(temp.path, cache);
  CHECK(references.empty());
}
#endif

TEST_CASE("a merged cluster shares one folder", "[organize]") {
  // Per-image name allocation collision-suffixed every member of a cluster
  // into its own folder (1400 folders for ~43 clusters on the acceptance
  // corpus); a cluster must allocate its name once.
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "a.png", "a");
  testutils::writeTextFile(temp.path / "b.png", "b");
  testutils::writeTextFile(temp.path / "c.png", "c");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["a.png"] = {.general = {tag("pink_hair", 0.9), tag("blue_eyes", 0.8)}};
  engine.byName["b.png"] = {.general = {tag("pink_hair", 0.88), tag("blue_eyes", 0.82)}};
  engine.byName["c.png"] = {.general = {tag("black_hair", 0.9), tag("brown_eyes", 0.85)}};
  features.byName["a.png"] = testutils::unitFeature(1.0);
  features.byName["b.png"] = testutils::unitFeature(0.99);
  features.byName["c.png"] = testutils::unitFeature(0.0);

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());

  auto unknownFolders = std::map<std::string, int>{};
  for (auto const& entry: fs::directory_iterator{temp.path / "organized"}) {
    auto const name = entry.path().filename().string();
    if (!name.starts_with("unknown_")) { continue; }
    unknownFolders[name] = static_cast<int>(
      std::ranges::count_if(fs::directory_iterator{entry.path()}, [](auto const&) {
        return true;
      })
    );
  }
  REQUIRE(unknownFolders.size() == 2);
  auto const members = std::vector<int>{
    unknownFolders.begin()->second,
    std::next(unknownFolders.begin())->second
  };
  CHECK((members == std::vector<int>{2, 1} || members == std::vector<int>{1, 2}));
}

// The fake-engine path loads no ONNX runtime, so it must stay notice-free
// while still running to completion (the spinner wiring must not change it).
TEST_CASE("organize with the fake engine prints no provider notice", "[organize]") {
  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics);
  auto const bytes = std::string{"miku-bytes"};
  testutils::writeTextFile(pics / "miku.png", bytes);
  auto const fixture = temp.path / "fixture.json";
  testutils::writeTextFile(
    fixture,
    std::format(
      R"({{"{}": {{"character": [["hatsune_miku", 0.9]]}}}})",
      core::sha256Hex(bytes)
    )
  );
  auto const fakeEngine = testutils::ScopedEnvVar{"ENCRO_FAKE_TAGGER", fixture.string()};

  auto cmd = CmdParseResult{};
  cmd.organizeDir = pics.string();
  auto exitCode = 1;
  auto const captured =
    testutils::captureStdout([&] { exitCode = organize::runOrganizeCommand(cmd); });

  CHECK(exitCode == 0);
  CHECK(captured.find("onnxruntime:") == std::string::npos);
}

// The loading spinner wraps real engine construction; the observable contract
// off a TTY is that the run still succeeds and exactly one provider notice
// survives the spinner's create-and-clear cycle.
TEST_CASE(
  "organize loads real engines and prints one provider notice",
  "[organize][real-model]"
) {
  auto const modelDirVar = processenv::readEnvVar("ENCRO_TEST_MODEL_DIR");
  if (!modelDirVar.has_value()) {
    SKIP("ENCRO_TEST_MODEL_DIR is unset; the real-model smoke needs a model dir.");
  }
  auto const modelDir = fs::path{*modelDirVar};
  if (
    !fs::exists(modelDir / "model.onnx")
    || !fs::exists(modelDir / "model_feat.onnx")
    || !fs::exists(modelDir / "selected_tags.csv")
  ) {
    SKIP("Model files not present; run --download-models for the real-model smoke.");
  }
  auto const ffmpeg = findFFmpeg(std::nullopt);
  if (!ffmpeg.has_value()) { SKIP("System FFmpeg not available on PATH."); }

  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics);
  auto const input = pics / "probe.png";
  auto const [exitCode, output, _, stderrText] = exec2(
    std::format(
      R"("{}" -hide_banner -loglevel error -y -f lavfi -i color=c=red:s=448x448 -frames:v 1 "{}")",
      quoteToolPath(*ffmpeg),
      input.string()
    )
  );
  REQUIRE(exitCode == 0);

  auto cmd = CmdParseResult{};
  cmd.organizeDir = pics.string();
  cmd.organizeModelDir = modelDir.string();
  cmd.ffmpegPath = ffmpeg->string();

  auto runExit = 1;
  auto const captured =
    testutils::captureStdout([&] { runExit = organize::runOrganizeCommand(cmd); });

  CHECK(runExit == 0);
  auto notices = std::size_t{0};
  for (
    auto pos = captured.find("onnxruntime:"); pos != std::string::npos;
    pos = captured.find("onnxruntime:", pos + 1)
  ) {
    ++notices;
  }
  CHECK(notices == 1);
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
  auto const text = organize::renderReport(report, 80);
  CHECK(text.find("hatsune_miku") != std::string::npos);
  CHECK(text.find("character tag") != std::string::npos);
  CHECK(text.find("scanned 2 images") != std::string::npos);
  // Report rules share the encode plan's glyph family (pipeline-narration).
  CHECK(text.find("\xE2\x94\x80") != std::string::npos);
  CHECK(text.find("---") == std::string::npos);
}

// The images column starts one past the folder column on every row; header
// included. Rows carry `images` right-aligned in 6 columns after that.
auto imagesColumnText(std::string const& line, std::size_t nameWidth) -> std::string {
  return line.substr(nameWidth + 1, 6);
}

TEST_CASE("renderReport truncates long folder names and stays aligned", "[organize]") {
  auto const longName = std::string{"unknown_animal_ears_eyepatch_black_hair"};
  REQUIRE(displaytext::displayWidth(longName) == 39);
  auto report = organize::ReportData{
    .folders = {
      organize::FolderReportLine{
        .folder = longName,
        .images = 2,
        .source = organize::FolderSource::NewCluster
      },
      organize::FolderReportLine{
        .folder = "venti",
        .images = 10,
        .source = organize::FolderSource::CharacterTag
      },
    },
  };

  // A 50-column terminal cannot fund more than the 30-column floor: the
  // 39-wide name truncates with an ellipsis while both rows keep the
  // header's columns.
  auto const text = organize::renderReport(report, 50);
  auto lines = std::vector<std::string>{};
  for (auto const& line: text | std::views::split('\n')) {
    lines.emplace_back(line.begin(), line.end());
  }
  REQUIRE(lines.size() >= 4);  // header, rule, two rows
  CHECK(text.find("...") != std::string::npos);
  CHECK(imagesColumnText(lines[0], 30) == "images");
  CHECK(imagesColumnText(lines[2], 30) == std::format("{: >6}", 2));
  CHECK(imagesColumnText(lines[3], 30) == std::format("{: >6}", 10));
  // The truncated cell fills the whole column (the count follows it
  // directly, not at the untruncated name's width).
  CHECK(displaytext::displayWidth(lines[2].substr(0, 30)) == 30);
}

TEST_CASE(
  "renderReport keeps the folder column at its minimum for short names",
  "[organize]"
) {
  auto report = organize::ReportData{
    .folders = {organize::FolderReportLine{
      .folder = "pikachu",
      .images = 1,
      .source = organize::FolderSource::FolderMatch
    }},
  };

  auto const text = organize::renderReport(report, 120);
  auto lines = std::vector<std::string>{};
  for (auto const& line: text | std::views::split('\n')) {
    lines.emplace_back(line.begin(), line.end());
  }
  // Floor 30: short names do not grow the column toward the 120-column
  // terminal, and the count stays right where today's fixed layout puts it.
  CHECK(imagesColumnText(lines[0], 30) == "images");
  CHECK(imagesColumnText(lines[2], 30) == std::format("{: >6}", 1));
  CHECK(text.find("folder ") != std::string::npos);
}

TEST_CASE("renderReport aligns a CJK folder name by display width", "[organize]") {
  auto report = organize::ReportData{
    .folders = {
      organize::FolderReportLine{
        .folder =
          "\xE5\x88\x9D\xE9\x9F\xB3\xE3\x83\x9F\xE3\x82\xAF",  // 初音ミク, width 8
        .images = 3,
        .source = organize::FolderSource::CharacterTag
      },
      organize::FolderReportLine{
        .folder = "venti",
        .images = 7,
        .source = organize::FolderSource::CharacterTag
      },
    },
  };

  // The folder column adapts by display width, and rows render in folder-name
  // order (the zero-row merge sorts), so venti precedes the CJK row.
  auto const text = organize::renderReport(report, 80);
  auto lines = std::vector<std::string>{};
  for (auto const& line: text | std::views::split('\n')) {
    lines.emplace_back(line.begin(), line.end());
  }
  // Padding counts display columns, not bytes: the CJK row's count sits at
  // the same column as every other row's.
  auto const cjkRow = lines[3];
  auto const countPos = cjkRow.find(std::format("{: >6}", 3));
  REQUIRE(countPos != std::string::npos);
  CHECK(displaytext::displayWidth(cjkRow.substr(0, countPos)) == 31);
  CHECK(imagesColumnText(lines[2], 30) == std::format("{: >6}", 7));
}

TEST_CASE("organize names failed copies in its report", "[organize]") {
  // A regular file where the output root belongs makes every copy fail through
  // the real staging-copy path on any platform: both ignored create_directories
  // calls cannot make way for the tree, so each item lands in the copy-failure
  // vector. The report must name the affected image; the exit code stays 0
  // (design D2), since nothing is corrupted and a re-run retries the copies.
  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics);
  auto const bytes = std::string{"miku-bytes"};
  auto const source = testutils::writeTextFile(pics / "miku.png", bytes);
  testutils::writeTextFile(pics / "organized", "not a directory");

  auto const fixture = temp.path / "fixture.json";
  testutils::writeTextFile(
    fixture,
    std::format(
      R"({{"{}": {{"character": [["hatsune_miku", 0.9]]}}}})",
      core::sha256Hex(bytes)
    )
  );
  auto const fakeEngine = testutils::ScopedEnvVar{"ENCRO_FAKE_TAGGER", fixture.string()};

  auto cmd = CmdParseResult{};
  cmd.organizeDir = pics.string();

  auto exitCode = 0;
  auto const captured =
    testutils::captureStdout([&] { exitCode = organize::runOrganizeCommand(cmd); });

  // The copy failed and was counted as such; the report names the source and
  // the destination that is missing from the tree.
  auto const destination = pics / "organized" / "hatsune_miku" / "miku.png";
  CHECK(captured.find("scanned 1 images: copied 0") != std::string::npos);
  CHECK(
    captured
      .find(std::format("copy failed: {} -> {}", source.string(), destination.string()))
    != std::string::npos
  );
  CHECK(exitCode == 0);
}

// A stop request is the organize run's own abort, not an error: the analysis
// phase reports it on the pipeline result, the copy phase stops before the
// next image, and the command prints one notice with the cancellation exit
// code instead of the report.
// ── Incremental organize (change organize-existing-structure tasks 4.1, 4.2,
// 5.1): sampling, demotion, and first-level teaching ──

// Sampling is capped and deterministic: members are ordered by content hash
// and the first kReferenceSampleSize analyzed, so two fresh directories with
// the same contents sample the same members, and a re-run pays nothing.
TEST_CASE("reference sampling caps members by content hash and caches", "[organize]") {
  auto const buildDir = [](fs::path const& dir) {
    for (auto index = 0; index < 21; ++index) {
      testutils::writeTextFile(
        dir / "角色A" / std::format("m{:02}.png", index),
        std::format("m{:02}", index)
      );
    }
    testutils::writeTextFile(dir / "new.png", "new");
  };

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  for (auto index = 0; index < 21; ++index) {
    auto const name = std::format("m{:02}.png", index);
    engine.byName[name] = {.general = {tag("pink_hair", 0.9)}};
    features.byName[name] = testutils::unitFeature(1.0);
  }
  engine.byName["new.png"] = {.general = {tag("pink_hair", 0.9)}};
  features.byName["new.png"] = testutils::unitFeature(0.95);

  auto const runOnce = [&](fs::path const& dir) {
    engine.classifiedPaths.clear();
    auto const report =
      organize::runOrganize(makeOptions(dir), engine, features, nullptr);
    REQUIRE(report.has_value());
    return std::set<std::string>{
      engine.classifiedPaths.begin(),
      engine.classifiedPaths.end()
    };
  };

  auto tempA = TempDir{};
  buildDir(tempA.path);
  auto const first = runOnce(tempA.path);
  // 20 of the 21 members plus the loose image.
  CHECK(first.size() == 21);
  CHECK(first.contains("new.png"));

  auto tempB = TempDir{};
  buildDir(tempB.path);
  CHECK(runOnce(tempB.path) == first);

  // The sampled analyses live in the shared cache, and a re-run performs no
  // inference at all.
  auto cache =
    organize::AnalysisCache{tempA.path / "organized" / ".cache" / "analysis.json"};
  cache.load();
  CHECK(cache.get(core::sha256Hex("m00")).has_value() == first.contains("m00.png"));
  auto const callsBefore = engine.calls.load();
  auto const rerun =
    organize::runOrganize(makeOptions(tempA.path), engine, features, nullptr);
  REQUIRE(rerun.has_value());
  CHECK(engine.calls.load() == callsBefore);

  // The reference folder itself is untouched; the loose image filed into the
  // mirrored skeleton.
  CHECK(fs::exists(tempA.path / "角色A" / "m00.png"));
  CHECK(fs::exists(tempA.path / "organized" / "角色A" / "new.png"));
}

TEST_CASE("a first-level folder teaches the run and is mirrored", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "角色A" / "member.png", "member");
  testutils::writeTextFile(temp.path / "solo.png", "solo");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["member.png"] = {.general = {tag("pink_hair", 0.9)}};
  engine.byName["solo.png"] = {.general = {tag("pink_hair", 0.9)}};
  features.byName["member.png"] = testutils::unitFeature(1.0);
  features.byName["solo.png"] = testutils::unitFeature(0.9);

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());
  CHECK(fs::exists(temp.path / "organized" / "角色A" / "solo.png"));
  // The first-level folder and its member are untouched; no parallel
  // unknown_ folder appears.
  CHECK(fs::exists(temp.path / "角色A" / "member.png"));
  CHECK_FALSE(hasUnknownFolder(*report));
}

// Demotion (design D4): a sample splitting into two identity clusters loses
// folder matching AND tag ownership, without changing the folder's contents.
TEST_CASE("a mixed sample demotes the folder from matching and ownership", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "温迪" / "a.png", "a");
  testutils::writeTextFile(temp.path / "温迪" / "b.png", "b");
  testutils::writeTextFile(temp.path / "new.png", "new");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["a.png"] = {.character = {tag("venti", 0.9)}};
  engine.byName["b.png"] = {.character = {tag("venti", 0.9)}};
  engine.byName["new.png"] = {.character = {tag("venti", 0.9)}};
  // Opposite features: two clusters under the combined tau.
  features.byName["a.png"] = testutils::unitFeature(1.0);
  features.byName["b.png"] = testutils::unitFeature(0.0);
  features.byName["new.png"] = testutils::unitFeature(1.0);

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());
  // Not demoted, the sampled sole-tag majority would own venti and file the
  // image under 温迪/; demoted, the raw tag folder is used.
  CHECK(fs::exists(temp.path / "organized" / "venti" / "new.png"));
  CHECK_FALSE(fs::exists(temp.path / "organized" / "温迪"));
  CHECK(fs::exists(temp.path / "温迪" / "a.png"));
  CHECK(fs::exists(temp.path / "温迪" / "b.png"));
}

TEST_CASE("a first-level folder owns its character tag", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "温迪" / "a.png", "a");
  testutils::writeTextFile(temp.path / "温迪" / "b.png", "b");
  testutils::writeTextFile(temp.path / "new.png", "new");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["a.png"] = {.character = {tag("venti", 0.9)}};
  engine.byName["b.png"] = {.character = {tag("venti", 0.9)}};
  engine.byName["new.png"] = {.character = {tag("venti", 0.9)}};
  // Close features: one cluster, no demotion.
  features.byName["a.png"] = testutils::unitFeature(1.0);
  features.byName["b.png"] = testutils::unitFeature(0.95);

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());
  CHECK(fs::exists(temp.path / "organized" / "温迪" / "new.png"));
  CHECK_FALSE(fs::exists(temp.path / "organized" / "venti"));
}

// Same-name sources merge (design D5): a first-level folder and an output-root
// folder of one name form a single reference whose membership is the union.
TEST_CASE(
  "same-name first-level and output folders merge into one reference",
  "[organize]"
) {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "角色A" / "s1.png", "s1");
  testutils::writeTextFile(temp.path / "角色A" / "s2.png", "s2");
  testutils::writeTextFile(temp.path / "organized" / "角色A" / "old.png", "old");
  testutils::writeTextFile(temp.path / "new.png", "new");

  // The output-root member carries venti from an earlier run; the sample
  // carries one venti and one xiao — only the union reaches a sole-tag
  // majority, so filing under 角色A proves the two sources merged.
  {
    auto cache =
      organize::AnalysisCache{temp.path / "organized" / ".cache" / "analysis.json"};
    cache.put(
      core::sha256Hex("old"),
      organize::AnalysisResult{
        .tags = {.character = {tag("venti", 0.9)}},
        .identity = testutils::unitFeature(1.0),
      }
    );
  }

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["s1.png"] = {.character = {tag("venti", 0.9)}};
  engine.byName["s2.png"] = {.character = {tag("xiao", 0.9)}};
  engine.byName["new.png"] = {.character = {tag("venti", 0.9)}};
  features.byName["s1.png"] = testutils::unitFeature(1.0);
  features.byName["s2.png"] = testutils::unitFeature(0.98);

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());
  CHECK(fs::exists(temp.path / "organized" / "角色A" / "new.png"));
  CHECK_FALSE(fs::exists(temp.path / "organized" / "venti"));
}

// New cluster names never reuse a folder that already exists — not at the
// first level, not under the output root (design D7): the suffixing is
// deterministic, and a zero-match reference folder still creates no skeleton.
TEST_CASE("a new cluster name never collides with an existing folder", "[organize]") {
  auto const run = [](fs::path const& dir) {
    testutils::writeTextFile(dir / "a.png", "a");
    testutils::writeTextFile(dir / "b.png", "b");
    auto engine = FakeTagger{};
    auto features = FakeFeatureEngine{};
    engine.byName["a.png"] = {.general = {tag("pink_hair", 0.9)}};
    engine.byName["b.png"] = {.general = {tag("pink_hair", 0.9)}};
    features.byName["a.png"] = testutils::unitFeature(1.0);
    features.byName["b.png"] = testutils::unitFeature(0.99);
    auto const report =
      organize::runOrganize(makeOptions(dir), engine, features, nullptr);
    REQUIRE(report.has_value());
  };

  SECTION("a first-level folder of that name") {
    auto temp = TempDir{};
    // Far-away feature and disagreeing tags: the folder captures nothing.
    testutils::writeTextFile(temp.path / "unknown_pink_hair" / "ref.png", "ref");
    auto engine = FakeTagger{};
    auto features = FakeFeatureEngine{};
    engine.byName["ref.png"] = {.general = {tag("black_hair", 0.9)}};
    features.byName["ref.png"] = testutils::unitFeature(-1.0);
    run(temp.path);
    CHECK(fs::exists(temp.path / "organized" / "unknown_pink_hair_2" / "a.png"));
    CHECK(fs::exists(temp.path / "organized" / "unknown_pink_hair_2" / "b.png"));
    CHECK_FALSE(fs::exists(temp.path / "organized" / "unknown_pink_hair"));
  }

  SECTION("an output-root folder of that name") {
    auto temp = TempDir{};
    // No cached analysis: the folder is not a reference, but its name still
    // must not be reused.
    testutils::writeTextFile(
      temp.path / "organized" / "unknown_pink_hair" / "old.png",
      "old"
    );
    run(temp.path);
    CHECK(fs::exists(temp.path / "organized" / "unknown_pink_hair_2" / "a.png"));
  }
}

#if defined(_WIN32)
// Strict lazy creation (design D7): the destination folder appears only
// once the copy's bytes reached the staging file, so a failed copy leaves no
// empty folder behind.
TEST_CASE("a failed copy leaves no empty destination folder", "[organize]") {
  auto temp = TempDir{};
  auto const source = testutils::writeTextFile(temp.path / "a.png", "content-a");
  auto items = std::vector<organize::ImageItem>{
    {.path = source, .contentHash = core::sha256Hex("content-a"), .folderName = "miku"},
  };

  auto const lock = ExclusivelyLockedFile{source};
  REQUIRE(lock.isLocked());

  auto const stats = organize::executeOrganize(temp.path, items, false);
  CHECK(stats.copied == 0);
  REQUIRE(stats.errors.size() == 1);
  CHECK_FALSE(fs::exists(temp.path / "organized" / "miku"));
}
#endif

// The disposition walk runs before the engines load (design D8): exactly one
// notice line names incremental mode and the reference count, recursive runs
// print nothing new, and virgin directories are not incremental.
TEST_CASE("incremental entry is announced once, and only incrementally", "[organize]") {
  auto const runCommand = [](fs::path const& dir, bool recursive) {
    auto cmd = CmdParseResult{};
    cmd.organizeDir = dir.string();
    cmd.recursive = recursive;
    auto exitCode = 1;
    auto const captured =
      testutils::captureStdout([&] { exitCode = organize::runOrganizeCommand(cmd); });
    return std::pair{exitCode, captured};
  };

  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics / "角色A");
  fs::create_directories(pics / "mix");
  auto const bytes = std::string{"miku-bytes"};
  testutils::writeTextFile(pics / "miku.png", bytes);
  testutils::writeTextFile(pics / "角色A" / "ref.png", "ref");
  auto const fixture = temp.path / "fixture.json";
  testutils::writeTextFile(
    fixture,
    std::format(
      R"({{"{}": {{"character": [["hatsune_miku", 0.9]]}}}})",
      core::sha256Hex(bytes)
    )
  );
  auto const fakeEngine = testutils::ScopedEnvVar{"ENCRO_FAKE_TAGGER", fixture.string()};

  auto const [exitCode, captured] = runCommand(pics, false);
  CHECK(exitCode == 0);
  CHECK(testutils::countOccurrences(captured, "incremental organize") == 1);
  CHECK(captured.find("1 reference folder") != std::string::npos);

  auto const [recursiveExit, recursiveCaptured] = runCommand(pics, true);
  CHECK(recursiveExit == 0);
  CHECK(recursiveCaptured.find("incremental organize") == std::string::npos);

  auto virgin = TempDir{};
  testutils::writeTextFile(virgin.path / "miku.png", bytes);
  auto const [virginExit, virginCaptured] = runCommand(virgin.path, false);
  CHECK(virginExit == 0);
  CHECK(virginCaptured.find("incremental organize") == std::string::npos);
}

// The report surface of an incremental run: the disposition list rides the
// ReportData, and zero-match references appear as zero rows.
TEST_CASE(
  "an incremental run reports dispositions and zero-match references",
  "[organize]"
) {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "角色A" / "member.png", "member");
  testutils::writeTextFile(temp.path / "角色B" / "member.png", "member-b");
  testutils::writeTextFile(temp.path / "mix" / "x.png", "x");
  testutils::writeTextFile(temp.path / "new.png", "new");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["member.png"] = {.general = {tag("pink_hair", 0.9)}};
  engine.byName["x.png"] = {.general = {tag("pink_hair", 0.9)}};
  engine.byName["new.png"] = {.general = {tag("pink_hair", 0.9)}};
  // 角色B's member disagrees on tags and feature: no capture, zero row.
  engine.byName["member-b.png"] = {.general = {tag("black_hair", 0.9)}};
  features.byName["member.png"] = testutils::unitFeature(1.0);
  features.byName["member-b.png"] = testutils::unitFeature(-1.0);
  features.byName["x.png"] = testutils::unitFeature(0.99);
  features.byName["new.png"] = testutils::unitFeature(0.95);

  auto const report =
    organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(report.has_value());
  REQUIRE(report->dispositions.size() == 3);
  CHECK(report->dispositions[0].folder == "mix");
  CHECK(report->dispositions[0].kind == organize::DispositionKind::Input);
  CHECK(report->dispositions[1].folder == "角色A");
  CHECK(report->dispositions[1].kind == organize::DispositionKind::Reference);
  CHECK_FALSE(report->dispositions[1].demoted);

  auto const text = organize::renderReport(*report, 80);
  // The zero row keeps the table's columns: the count sits at the same
  // display column as every counted row.
  auto zeroRow = std::string{};
  for (auto const& line: text | std::views::split('\n')) {
    auto const row = std::string{line.begin(), line.end()};
    if (row.find("角色B") == 0) {
      zeroRow = row;
      break;
    }
  }
  REQUIRE_FALSE(zeroRow.empty());
  auto const countPos = zeroRow.find("     0");
  REQUIRE(countPos != std::string::npos);
  CHECK(displaytext::displayWidth(zeroRow.substr(0, countPos)) == 31);
  CHECK(text.find("角色B: reference") != std::string::npos);
}

TEST_CASE(
  "organize reports a stop request instead of an error or a report",
  "[organize][stop-signal]"
) {
  auto const stopGuard = testutils::ScopedStopSignalReset{};

  SECTION("analysis abort") {
    auto temp = TempDir{};
    testutils::writeTextFile(temp.path / "miku.png", "miku");
    auto engine = FakeTagger{};
    auto features = FakeFeatureEngine{};
    engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};

    stopsignal::requestStop();
    auto const result = runOrganize(makeOptions(temp.path), engine, features, nullptr);

    REQUIRE(result.has_value());
    CHECK(result->canceled);
    CHECK(result->copied == 0);
    CHECK_FALSE(fs::exists(temp.path / "organized"));
  }

  SECTION("copy abort on a resumed run") {
    auto temp = TempDir{};
    testutils::writeTextFile(temp.path / "miku.png", "miku");
    auto engine = FakeTagger{};
    auto features = FakeFeatureEngine{};
    engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};

    // First run analyzes and copies: the cache now covers the image, so the
    // stopped run has nothing to analyze and the stop lands in the copy loop.
    auto const first = runOrganize(makeOptions(temp.path), engine, features, nullptr);
    REQUIRE(first.has_value());
    REQUIRE(first->copied == 1);

    stopsignal::requestStop();
    auto const result = runOrganize(makeOptions(temp.path), engine, features, nullptr);

    REQUIRE(result.has_value());
    CHECK(result->canceled);
  }

  SECTION("command boundary") {
    auto temp = TempDir{};
    auto const pics = temp.path / "pics";
    fs::create_directories(pics);
    testutils::writeTextFile(pics / "miku.png", "miku");
    auto const fixture = temp.path / "fixture.json";
    testutils::writeTextFile(fixture, "{}");
    auto const fakeEngine =
      testutils::ScopedEnvVar{"ENCRO_FAKE_TAGGER", fixture.string()};

    auto cmd = CmdParseResult{};
    cmd.organizeDir = pics.string();

    stopsignal::requestStop();
    auto exitCode = 0;
    auto const stderrPath = temp.path / "stderr.txt";
    auto const captured = testutils::captureStdout([&] {
      auto const capture = testutils::StderrCapture{stderrPath};
      exitCode = organize::runOrganizeCommand(cmd);
    });

    CHECK(exitCode == stopsignal::kCanceledExitCode);
    CHECK(
      testutils::countOccurrences(
        testutils::readTextFile(stderrPath),
        "Organize canceled by user."
      )
      == 1
    );
    CHECK(captured.find("images: copied") == std::string::npos);
  }
}
