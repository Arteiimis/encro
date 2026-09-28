// Routing, vectors, clustering, and teaching (tasks 3.1-3.4).
#include "organize/assign.h"
#include "organize/cache.h"
#include "organize/cluster.h"
#include "core/sha256.h"
#include "organize/teach.h"

#include "test_utils.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
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
  std::vector<organize::TagScore> character = {},
  std::vector<float> identity = {}
) -> organize::AnalysisResult {
  return organize::AnalysisResult{
    .tags =
      {.general = std::move(general), .character = std::move(character), .rating = {}},
    .identity = std::move(identity),
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

TEST_CASE("positiveCharacterTags keeps identity-bearing candidates", "[organize]") {
  auto const result = analysis({}, {tag("a", 0.5), tag("b", 0.9), tag("c", 0.3)});
  auto const tags = organize::positiveCharacterTags(result);
  // Exactly-0.5 confidences are zero logit = zero evidence; excluded.
  REQUIRE(tags.size() == 1);
  CHECK(tags[0].tag == "b");
}

TEST_CASE("hasStrongCountTag detects asserted count tags", "[organize]") {
  CHECK(organize::hasStrongCountTag(analysis({tag("2girls", 0.9)})));
  // 0.5-band confidences are zero-evidence noise and must not count.
  CHECK(!organize::hasStrongCountTag(analysis({tag("2girls", 0.5)})));
  CHECK(!organize::hasStrongCountTag(analysis({tag("2girls", 0.6)})));
  CHECK(!organize::hasStrongCountTag(analysis({tag("1girl", 0.9)})));
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

TEST_CASE("cosineSimilarity is 1 for identical, 0 for disjoint", "[organize]") {
  CHECK(
    organize::cosineSimilarity(testutils::unitFeature(1.0), testutils::unitFeature(1.0))
    == Catch::Approx(1.0)
  );
  // Orthogonal only up to the float rounding of cos(pi/2).
  CHECK(
    organize::cosineSimilarity(testutils::unitFeature(1.0), testutils::unitFeature(0.0))
    == Catch::Approx(0.0).margin(1e-6)
  );
  CHECK(
    organize::cosineSimilarity(testutils::unitFeature(1.0), testutils::unitFeature(-1.0))
    == Catch::Approx(-1.0)
  );
  CHECK(
    organize::cosineSimilarity(testutils::unitFeature(1.0), std::span<float const>{})
    == 0.0
  );
  CHECK(organize::cosineSimilarity(std::span<float const>{}, {}) == 0.0);
}

TEST_CASE("clusterPending groups features that reach the threshold", "[organize]") {
  // One character across styles: both mates sit at cos 0.9 or above against
  // the cluster centroid (kIdentityTau is 0.643).
  auto items = std::vector<organize::ImageItem>{
    item("a", analysis({}, {}, testutils::unitFeature(1.0))),
    item("b", analysis({}, {}, testutils::unitFeature(0.9))),
    item("c", analysis({}, {}, testutils::unitFeature(0.1))),
  };
  auto const clusters = organize::clusterPending(items, {0, 1, 2});
  REQUIRE(clusters.size() == 2);
  // Hash order decides which cluster is first; the pair is the big one.
  auto const& merged = clusters[0].itemIndices.size() == 2 ? clusters[0] : clusters[1];
  CHECK(merged.itemIndices.size() == 2);
}

TEST_CASE("a feature below the threshold opens a new cluster", "[organize]") {
  // kIdentityTau sits at 0.643050, the cosine equivalent of the identity
  // model's published metric threshold: 0.7 belongs to the character, 0.6
  // does not.
  auto const above = organize::clusterPending(
    std::vector<organize::ImageItem>{
      item("a", analysis({}, {}, testutils::unitFeature(1.0))),
      item("b", analysis({}, {}, testutils::unitFeature(0.7))),
    },
    {0, 1}
  );
  CHECK(above.size() == 1);

  auto const below = organize::clusterPending(
    std::vector<organize::ImageItem>{
      item("a", analysis({}, {}, testutils::unitFeature(1.0))),
      item("b", analysis({}, {}, testutils::unitFeature(0.6))),
    },
    {0, 1}
  );
  CHECK(below.size() == 2);
}

TEST_CASE("an item without an identity feature never joins a cluster", "[organize]") {
  // A failed analysis (or an unavailable identity model) leaves the feature
  // empty; the item falls through to uncategorized instead of clustering.
  auto items = std::vector<organize::ImageItem>{
    item("a", analysis({tag("pink_hair", 0.9)})),
    item("b", analysis({tag("pink_hair", 0.9)}, {}, testutils::unitFeature(1.0))),
  };
  auto const clusters = organize::clusterPending(items, {0, 1});
  REQUIRE(clusters.size() == 1);
  CHECK(clusters[0].itemIndices == std::vector<std::size_t>{1});
}

TEST_CASE(
  "clusterPending opens a new cluster below tau and names distinctly",
  "[organize]"
) {
  auto items = std::vector<organize::ImageItem>{
    item("a", analysis({tag("pink_hair", 0.9)}, {}, testutils::unitFeature(1.0))),
    item("b", analysis({tag("pink_hair", 0.88)}, {}, testutils::unitFeature(0.99))),
    item("c", analysis({tag("black_hair", 0.9)}, {}, testutils::unitFeature(0.0))),
  };
  auto const clusters = organize::clusterPending(items, {0, 1, 2});
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

TEST_CASE("clusterFolderName ranks member tags by how many carry them", "[organize]") {
  // A tag two members carry outranks the single most confident tag: the name
  // describes the cluster, not its loudest image (design D5).
  auto items = std::vector<organize::ImageItem>{
    item(
      "a",
      analysis(
        {tag("blue_eyes", 0.99), tag("pink_hair", 0.60)},
        {},
        testutils::unitFeature(1.0)
      )
    ),
    item("b", analysis({tag("pink_hair", 0.61)}, {}, testutils::unitFeature(0.99))),
  };
  auto const clusters = organize::clusterPending(items, {0, 1});
  REQUIRE(clusters.size() == 1);

  auto used = std::set<std::string>{};
  CHECK(
    organize::clusterFolderName(clusters[0], items, used) == "unknown_pink_hair_blue_eyes"
  );
}

TEST_CASE(
  "a cluster with no identity tag in its members falls back to a hash name",
  "[organize]"
) {
  // The identity model has no vocabulary, so naming reads the members' tags;
  // when none of them is identity-bearing the name stays deterministic.
  auto items = std::vector<organize::ImageItem>{
    item(
      "a",
      analysis(
        {tag("1girl", 0.9), tag("solo", 0.8)},
        {tag("miku", 0.9)},
        testutils::unitFeature(1.0)
      )
    ),
  };
  auto const clusters = organize::clusterPending(items, {0});
  REQUIRE(clusters.size() == 1);

  auto first = std::set<std::string>{};
  auto second = std::set<std::string>{};
  auto const name = organize::clusterFolderName(clusters[0], items, first);
  CHECK(name.starts_with("unknown_char_"));
  CHECK(organize::clusterFolderName(clusters[0], items, second) == name);
}

TEST_CASE("buildFolderReferences skips cache misses and the cache dir", "[organize]") {
  auto temp = TempDir{};
  auto cachePath = temp.path / "organized" / ".cache" / "analysis.json";
  auto cache = organize::AnalysisCache{cachePath};
  cache.load();
  cache.put(
    core::sha256Hex("member-bytes"),
    analysis({tag("pink_hair", 0.9)}, {tag("miku", 0.9)}, testutils::unitFeature(1.0))
  );
  cache.put(core::sha256Hex("empty-bytes"), organize::AnalysisResult{});

  auto const folder = temp.path / "organized" / "unknown_pink";
  fs::create_directories(folder);
  testutils::writeTextFile(folder / "member.png", "member-bytes");
  testutils::writeTextFile(folder / "cached-empty.png", "empty-bytes");
  testutils::writeTextFile(folder / "unknown.png", "not-in-cache");
  fs::create_directories(temp.path / "organized" / ".cache");
  testutils::writeTextFile(temp.path / "organized" / ".cache" / "analysis.json", "{}");

  auto const references = organize::buildFolderReferences(temp.path, cache);
  REQUIRE(references.size() == 1);
  CHECK(references[0].name == "unknown_pink");
  CHECK(references[0].analyzableMembers == 2);
  CHECK(references[0].soleTagCounts.at("miku") == 1);
  // The cached-empty member contributes no feature, so the mean is the one
  // member that has one.
  CHECK(references[0].featureMembers == 1);
  REQUIRE(references[0].meanFeature.size() == 2);
  CHECK(
    organize::cosineSimilarity(references[0].meanFeature, testutils::unitFeature(1.0))
    == Catch::Approx(1.0)
  );
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

  auto const references = organize::buildFolderReferences(temp.path, cache);
  REQUIRE(references.size() == 1);
  CHECK(references[0].name == "初音ミク");
  REQUIRE(organize::claimedTag(references[0]) == "hatsune_miku");

  auto const* owner = organize::owningFolder(references, "hatsune_miku");
  REQUIRE(owner != nullptr);
  CHECK(owner->name == "初音ミク");
}
