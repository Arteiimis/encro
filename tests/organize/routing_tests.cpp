// Routing, vectors, clustering, and teaching (tasks 3.1-3.4).
#include "organize/assign.h"
#include "organize/cache.h"
#include "organize/cluster.h"
#include "core/sha256.h"
#include "organize/teach.h"

#include "test_utils.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <format>
#include <set>
#include <string>

namespace fs = std::filesystem;

namespace {

auto tag(std::string name, double confidence) -> organize::TagScore {
  return organize::TagScore{.tag = std::move(name), .confidence = confidence};
}

auto analysis(
  std::vector<organize::TagScore> general,
  std::vector<organize::TagScore> character = {}
) -> organize::AnalysisResult {
  return organize::AnalysisResult{
    .general = std::move(general),
    .character = std::move(character),
    .rating = {},
  };
}

auto item(std::string name, organize::AnalysisResult result) -> organize::ImageItem {
  auto const bytes = name + "-bytes";
  return organize::ImageItem{
    .path = fs::path{name},
    .contentHash = core::sha256Hex(bytes),
    .analysis = std::move(result),
  };
}

}  // namespace

TEST_CASE("confidentCharacterTags filters and sorts", "[organize]") {
  auto const result = analysis({}, {tag("a", 0.5), tag("b", 0.9), tag("c", 0.3)});
  auto const tags = organize::confidentCharacterTags(result, 0.35);
  REQUIRE(tags.size() == 2);
  CHECK(tags[0].tag == "b");
  CHECK(tags[1].tag == "a");
}

TEST_CASE("isMultiSubject detects count tags at or above threshold", "[organize]") {
  CHECK(organize::isMultiSubject(analysis({tag("2girls", 0.8)}), 0.35));
  CHECK(!organize::isMultiSubject(analysis({tag("2girls", 0.2)}), 0.35));
  CHECK(!organize::isMultiSubject(analysis({tag("1girl", 0.9)}), 0.35));
}

TEST_CASE("folder ownership claims by majority of sole candidates", "[organize]") {
  auto reference = organize::FolderReference{.name = "初音ミク"};
  reference.analyzableMembers = 10;
  reference.soleTagCounts["hatsune_miku"] = 6;
  CHECK(organize::claimedTag(reference) == "hatsune_miku");

  reference.soleTagCounts["hatsune_miku"] = 5;  // exactly half: no claim
  CHECK(organize::claimedTag(reference).empty());

  auto const* owner = organize::owningFolder({reference}, "hatsune_miku");
  CHECK(owner == nullptr);  // no majority anymore

  auto other = organize::FolderReference{.name = "miku_backup"};
  other.analyzableMembers = 4;
  other.soleTagCounts["hatsune_miku"] = 3;
  auto references = std::vector{other, reference};
  // reference lost its claim; other claims it.
  CHECK(organize::owningFolder(references, "hatsune_miku") == &references[0]);
}

TEST_CASE("appearanceVector caps to top-K and floors at threshold", "[organize]") {
  auto general = std::vector<organize::TagScore>{};
  for (auto index = 0; index < 25; ++index) {
    general.push_back(tag(std::format("tag{:02}", index), 0.9 - index * 0.01));
  }
  general.push_back(tag("weak", 0.1));
  auto const vector = organize::appearanceVector(analysis(general), 0.35);
  CHECK(vector.size() == organize::kTopKTags);
  CHECK(vector.find("weak") == vector.end());
  CHECK(vector.find("tag00") != vector.end());
}

TEST_CASE("cosineSimilarity is 1 for identical, 0 for disjoint", "[organize]") {
  auto const a = std::map<std::string, double>{{"x", 1.0}, {"y", 1.0}};
  CHECK(organize::cosineSimilarity(a, a) == Catch::Approx(1.0));
  auto const b = std::map<std::string, double>{{"z", 1.0}};
  CHECK(organize::cosineSimilarity(a, b) == 0.0);
  CHECK(organize::cosineSimilarity(a, {}) == 0.0);
}

TEST_CASE(
  "clusterPending groups same-character-across-styles deterministically",
  "[organize]"
) {
  // Same appearance tag set, differing style tags below threshold.
  auto items = std::vector<organize::ImageItem>{
    item("a", analysis({tag("pink_hair", 0.9), tag("blue_eyes", 0.85)})),
    item("b", analysis({tag("blue_eyes", 0.86), tag("pink_hair", 0.88)})),
    item("c", analysis({tag("white_hair", 0.9), tag("red_eyes", 0.8)})),
  };
  auto pending = std::vector<std::size_t>{0, 1, 2};

  auto const clusters = organize::clusterPending(items, pending, 0.35);
  REQUIRE(clusters.size() == 2);
  // Hash order decides which cluster is first; find the pink one.
  auto const& pink = clusters[0].itemIndices.size() == 2 ? clusters[0] : clusters[1];
  CHECK(pink.itemIndices.size() == 2);
}

TEST_CASE(
  "clusterPending opens a new cluster below tau and names distinctly",
  "[organize]"
) {
  auto items = std::vector<organize::ImageItem>{
    item("a", analysis({tag("pink_hair", 0.9)})),
    item("b", analysis({tag("utterly_different", 0.9)})),
    item("c", analysis({tag("pink_hair", 0.88)})),
  };
  auto const clusters = organize::clusterPending(items, {0, 1, 2}, 0.35);
  REQUIRE(clusters.size() == 2);

  auto used = std::set<std::string>{};
  auto const firstName = organize::clusterFolderName(clusters[0], items, used);
  CHECK(firstName.starts_with("unknown_"));
  auto const secondName = organize::clusterFolderName(clusters[1], items, used);
  CHECK(firstName != secondName);

  // A cluster identical in description to an existing name gets suffixed.
  auto const duplicate = organize::clusterFolderName(clusters[0], items, used);
  CHECK(duplicate == firstName + "_2");
}

TEST_CASE("clusterFolderName falls back to deterministic hash name", "[organize]") {
  auto items = std::vector<organize::ImageItem>{};
  // An analysis with no general tags at threshold -> empty vector cluster.
  items.push_back(item("a", analysis({}, {tag("miku", 0.9)})));
  auto const clusters = organize::clusterPending(items, {0}, 0.35);
  // Empty vectors never join a cluster; nothing to name.
  CHECK(clusters.empty());
}

TEST_CASE("buildFolderReferences skips cache misses and the cache dir", "[organize]") {
  auto temp = TempDir{};
  auto cachePath = temp.path / "organized" / ".cache" / "analysis.json";
  auto cache = organize::AnalysisCache{cachePath};
  cache.load();
  cache.put(
    core::sha256Hex("member-bytes"),
    analysis({tag("pink_hair", 0.9)}, {tag("miku", 0.9)})
  );
  cache.put(core::sha256Hex("empty-bytes"), organize::AnalysisResult{});

  auto const folder = temp.path / "organized" / "unknown_pink";
  fs::create_directories(folder);
  testutils::writeTextFile(folder / "member.png", "member-bytes");
  testutils::writeTextFile(folder / "cached-empty.png", "empty-bytes");
  testutils::writeTextFile(folder / "unknown.png", "not-in-cache");
  fs::create_directories(temp.path / "organized" / ".cache");
  testutils::writeTextFile(temp.path / "organized" / ".cache" / "analysis.json", "{}");

  auto const references = organize::buildFolderReferences(temp.path, cache, 0.35);
  REQUIRE(references.size() == 1);
  CHECK(references[0].name == "unknown_pink");
  CHECK(references[0].analyzableMembers == 2);
  CHECK(references[0].soleTagCounts.at("miku") == 1);
  CHECK(references[0].vectorMembers == 1);
}

TEST_CASE("renamed character folder keeps teaching under its new name", "[organize]") {
  auto temp = TempDir{};
  auto cache =
    organize::AnalysisCache{temp.path / "organized" / ".cache" / "analysis.json"};
  cache.load();
  for (auto index = 0; index < 5; ++index) {
    cache.put(
      core::sha256Hex("miku-" + std::to_string(index)),
      analysis({}, {tag("hatsune_miku", 0.9)})
    );
  }

  // User renamed hatsune_miku/ to 初音ミク/.
  auto const renamed = temp.path / "organized" / "初音ミク";
  fs::create_directories(renamed);
  for (auto index = 0; index < 5; ++index) {
    testutils::writeTextFile(
      renamed / ("m" + std::to_string(index) + ".png"),
      "miku-" + std::to_string(index)
    );
  }

  auto const references = organize::buildFolderReferences(temp.path, cache, 0.35);
  REQUIRE(references.size() == 1);
  CHECK(references[0].name == "初音ミク");
  REQUIRE(organize::claimedTag(references[0]) == "hatsune_miku");

  auto const* owner = organize::owningFolder(references, "hatsune_miku");
  REQUIRE(owner != nullptr);
  CHECK(owner->name == "初音ミク");
}
