// Pipeline integration over the FakeTagger seam (tasks 4.1-4.4): staging,
// mixed/uncategorized semantics, dry-run, recluster, resume, rename teaching.
#include "core/progress.h"
#include "core/sha256.h"

#include "organize/cache.h"
#include "organize/cluster.h"
#include "organize/pipeline.h"
#include "organize/teach.h"

#include "cmd/cmd.h"
#include "infra/stop_signal.h"
#include "organize/organize_command.h"
#include "tagger/mapping.h"

#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <filesystem>
#include <format>
#include <map>
#include <string>

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

  auto classify(fs::path const& path) -> eh::Result<tagger::TaggerOutput> override {
    ++calls;
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

  (void)organize::runOrganize(makeOptions(temp.path), engine, features, nullptr);
  REQUIRE(engine.calls.load() == 1);

  auto options = makeOptions(temp.path);
  options.recluster = true;
  (void)organize::runOrganize(options, engine, features, nullptr);
  CHECK(engine.calls.load() == 2);
}

TEST_CASE("renamed character folder teaches subsequent runs", "[organize]") {
  auto temp = TempDir{};
  testutils::writeTextFile(temp.path / "miku.png", "miku");

  auto engine = FakeTagger{};
  auto features = FakeFeatureEngine{};
  engine.byName["miku.png"] = {.character = {tag("hatsune_miku", 0.9)}};

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
  // Report rules share the encode plan's glyph family (pipeline-narration).
  CHECK(text.find("\xE2\x94\x80") != std::string::npos);
  CHECK(text.find("---") == std::string::npos);
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
