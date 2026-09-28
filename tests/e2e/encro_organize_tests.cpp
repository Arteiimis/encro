// organize e2e (task 5.2): full CLI runs against the in-process fake tagger
// seam (ENCRO_FAKE_TAGGER), the fake_media_tool pattern applied to model
// inference. No network, no model files.
#include "e2e_test_utils.h"

#include "core/sha256.h"
#include "test_utils.h"

#include <boost/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

auto sha256Of(std::string_view bytes) -> std::string {
  return core::sha256Hex(bytes);
}

void writeImage(fs::path const& path, std::string_view bytes) {
  auto out = std::ofstream{path, std::ios::binary};
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Fixture: what the fake engines answer for one image's content hash. An image
// may carry tags and an identity feature at once, so its object collects
// whichever fields that image has.
void writeFixture(
  fs::path const& path,
  std::map<std::string, std::pair<std::string, double>> const& characterTags,
  std::map<std::string, std::pair<std::string, double>> const& generalTags,
  std::map<std::string, std::vector<float>> const& identities = {}
) {
  auto fixture = boost::json::object{};
  auto entryFor = [&](std::string const& name) -> boost::json::object& {
    auto& value = fixture[sha256Of(name)];
    if (!value.is_object()) { value = boost::json::object{}; }
    return value.as_object();
  };
  // A `json::array` element goes through `value` on purpose: a braced
  // single same-type element (array{array{…}}) picks the copy constructor
  // on some compilers (CI's gcc) and the initializer_list constructor on
  // others (clang-cl), which flattens [[tag, conf]] to [tag, conf] — a
  // fixture the fake engines parse as zero tags. Going through `value`
  // makes the nesting the only valid reading everywhere.
  auto scoredTag = [](std::pair<std::string, double> const& tag) {
    return boost::json::value{boost::json::array{tag.first, tag.second}};
  };
  for (auto const& [name, tag]: characterTags) {
    entryFor(name)["character"] = boost::json::array{scoredTag(tag)};
  }
  for (auto const& [name, tag]: generalTags) {
    entryFor(name)["general"] = boost::json::array{scoredTag(tag)};
  }
  for (auto const& [name, feature]: identities) {
    auto numbers = boost::json::array{};
    for (auto const value: feature) { numbers.emplace_back(value); }
    entryFor(name)["identity"] = std::move(numbers);
  }

  auto out = std::ofstream{path, std::ios::binary};
  out << boost::json::serialize(fixture);
}

}  // namespace

TEST_CASE("organize groups images by character via the CLI", "[e2e][organize]") {
  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics);
  writeImage(pics / "miku1.png", "miku1");
  writeImage(pics / "miku2.png", "miku2");
  writeImage(pics / "oc1.png", "oc1");

  auto fixture = temp.path / "fixture.json";
  writeFixture(
    fixture,
    {{"miku1", {"hatsune_miku", 0.9}}, {"miku2", {"hatsune_miku", 0.85}}},
    {{"oc1", {"pink_hair", 0.9}}},
    // Clustering weighs the identity feature with identity-bearing general
    // tags (0.8/0.2, design D1/D3), so oc1's pink_hair both joins the score
    // and names the folder it ends up in.
    {{"oc1", {1.0F, 0.0F}}}
  );

  auto const run = e2e::runEncro(
    {"organize", pics.string()},
    std::nullopt,
    {{"ENCRO_FAKE_TAGGER", fixture.string()}}
  );
  REQUIRE_SUCCESS(run);
  CHECK(fs::exists(pics / "organized" / "hatsune_miku" / "miku1.png"));
  CHECK(fs::exists(pics / "organized" / "hatsune_miku" / "miku2.png"));
  // Single-subject remainder clustered into an unknown_ folder.
  auto foundCluster = false;
  for (auto const& entry: fs::directory_iterator{pics / "organized"}) {
    if (entry.path().filename().string().starts_with("unknown_")) {
      foundCluster = fs::exists(entry.path() / "oc1.png");
    }
  }
  CHECK(foundCluster);
  // Report names the sources and the originals stay put.
  CHECK(run.stdoutText.find("character tag") != std::string::npos);
  CHECK(run.stdoutText.find("new cluster") != std::string::npos);
  CHECK(fs::exists(pics / "miku1.png"));
}

TEST_CASE("organize dry run prints the plan without copying", "[e2e][organize]") {
  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics);
  writeImage(pics / "miku1.png", "miku1");

  auto fixture = temp.path / "fixture.json";
  writeFixture(fixture, {{"miku1", {"hatsune_miku", 0.9}}}, {});

  auto const run = e2e::runEncro(
    {"organize", pics.string(), "--dry-run"},
    std::nullopt,
    {{"ENCRO_FAKE_TAGGER", fixture.string()}}
  );
  REQUIRE_SUCCESS(run);
  CHECK(run.stdoutText.find("hatsune_miku") != std::string::npos);
  CHECK(!fs::exists(pics / "organized" / "hatsune_miku"));
}

TEST_CASE("organize re-run resumes from the cache", "[e2e][organize]") {
  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics);
  writeImage(pics / "miku1.png", "miku1");
  writeImage(pics / "miku2.png", "miku2");

  auto fixture = temp.path / "fixture.json";
  writeFixture(
    fixture,
    {{"miku1", {"hatsune_miku", 0.9}}, {"miku2", {"hatsune_miku", 0.85}}},
    {}
  );

  auto const env =
    std::map<std::string, std::string>{{{"ENCRO_FAKE_TAGGER", fixture.string()}}};
  (void)e2e::runEncro({"organize", pics.string()}, std::nullopt, env);

  auto const second = e2e::runEncro({"organize", pics.string()}, std::nullopt, env);
  REQUIRE_SUCCESS(second);
  CHECK(second.stdoutText.find("(2 cache hits)") != std::string::npos);
  CHECK(second.stdoutText.find("skipped existing 2") != std::string::npos);
}

TEST_CASE("renamed character folder teaches a later e2e run", "[e2e][organize]") {
  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics);
  writeImage(pics / "miku1.png", "miku1");

  auto fixture = temp.path / "fixture.json";
  writeFixture(
    fixture,
    {{"miku1", {"hatsune_miku", 0.9}}, {"miku2", {"hatsune_miku", 0.9}}},
    {}
  );

  auto const env =
    std::map<std::string, std::string>{{{"ENCRO_FAKE_TAGGER", fixture.string()}}};
  (void)e2e::runEncro({"organize", pics.string()}, std::nullopt, env);

  fs::rename(pics / "organized" / "hatsune_miku", pics / "organized" / "初音ミク");
  writeImage(pics / "miku2.png", "miku2");

  auto const second = e2e::runEncro({"organize", pics.string()}, std::nullopt, env);
  REQUIRE_SUCCESS(second);
  CHECK(fs::exists(pics / "organized" / "初音ミク" / "miku2.png"));
  CHECK(!fs::exists(pics / "organized" / "hatsune_miku"));
}

TEST_CASE("missing models fail fast with guidance", "[e2e][organize]") {
  auto temp = TempDir{};
  auto const pics = temp.path / "pics";
  fs::create_directories(pics);
  writeImage(pics / "miku1.png", "miku1");
  auto const emptyModels = temp.path / "models";
  fs::create_directories(emptyModels);

  auto const run =
    e2e::runEncro({"organize", pics.string(), "--model-dir", emptyModels.string()});
  CHECK(run.exitCode != 0);
  CHECK(
    (run.stdoutText.find("models not found") != std::string::npos
     || run.stderrText.find("models not found") != std::string::npos)
  );
}

// The fetch-only form: --download-models stands alone, so no directory is
// needed and nothing is scanned. The child's working directory is this temp
// dir, so a silent fallback to "." would scan it and leave an organized/ tree
// right here — which is what the assertion below watches.
TEST_CASE("organize fetches models without a directory", "[e2e][organize]") {
  auto temp = TempDir{};
  writeImage(temp.path / "miku1.png", "miku1");

  auto fixture = temp.path / "fixture.json";
  writeFixture(fixture, {{"miku1", {"hatsune_miku", 0.9}}}, {});

  auto const run = e2e::runEncro(
    {"organize", "--download-models"},
    temp.path,
    {{"ENCRO_FAKE_TAGGER", fixture.string()}}
  );
  REQUIRE_SUCCESS(run);
  CHECK(!fs::exists(temp.path / "organized"));
}

// The rule is hand-written (design D1), so the rendered error and the help
// hint are what could regress silently; the parse-level case pins the message
// text alone.
TEST_CASE("organize without a directory names the missing argument", "[e2e][organize]") {
  auto const run = e2e::runEncro({"organize"});
  CHECK(run.exitCode == 1);
  CHECK(
    run.stderrText.find("error: Invalid arguments: dir is required") != std::string::npos
  );
  CHECK(
    run.stderrText.find("hint: Run encro -h for help (or -hh for all options).")
    != std::string::npos
  );
}
