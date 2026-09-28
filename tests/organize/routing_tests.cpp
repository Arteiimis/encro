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
  // the cluster centroid, in feature-only mode (no identity tags here, so the
  // comparison uses the feature-only default).
  auto items = std::vector<organize::ImageItem>{
    item("a", analysis({}, {}, testutils::unitFeature(1.0))),
    item("b", analysis({}, {}, testutils::unitFeature(0.9))),
    item("c", analysis({}, {}, testutils::unitFeature(0.1))),
  };
  auto const clusters = organize::clusterPending(items, {0, 1, 2}).clusters;
  REQUIRE(clusters.size() == 2);
  // Hash order decides which cluster is first; the pair is the big one.
  auto const& merged = clusters[0].itemIndices.size() == 2 ? clusters[0] : clusters[1];
  CHECK(merged.itemIndices.size() == 2);
}

TEST_CASE("a feature below the feature-only default opens a new cluster", "[organize]") {
  // With no identity-tag evidence the comparison is a bare cosine and the
  // calibrated default for that mode is 0.74 (design D6): 0.8 belongs to the
  // character, 0.7 does not.
  auto const above = organize::clusterPending(
                       std::vector<organize::ImageItem>{
                         item("a", analysis({}, {}, testutils::unitFeature(1.0))),
                         item("b", analysis({}, {}, testutils::unitFeature(0.8))),
                       },
                       {0, 1}
  )
                       .clusters;
  CHECK(above.size() == 1);

  auto const below = organize::clusterPending(
                       std::vector<organize::ImageItem>{
                         item("a", analysis({}, {}, testutils::unitFeature(1.0))),
                         item("b", analysis({}, {}, testutils::unitFeature(0.7))),
                       },
                       {0, 1}
  )
                       .clusters;
  CHECK(below.size() == 2);
}

TEST_CASE("the combined score weights the feature and the tags", "[organize]") {
  // 0.8/0.2 (design D2) over the two cosines. The tag side is built from
  // identity-bearing general tags at or above the naming floor, weighted by
  // confidence and normalized — no corpus statistic (design D1).
  auto const image = organize::profileOf(item(
    "a",
    analysis(
      {tag("pink_hair", 0.9), tag("blue_eyes", 0.3), tag("2girls", 0.95)},
      {},
      testutils::unitFeature(1.0)
    )
  ));
  // blue_eyes is below the naming floor and 2girls is not an identity tag.
  REQUIRE(image.tags.size() == 1);
  CHECK(organize::tagCosine(image.tags, image.tags) == Catch::Approx(1.0));

  auto const mate = organize::profileOf(
    item("b", analysis({tag("pink_hair", 0.9)}, {}, testutils::unitFeature(0.5)))
  );
  auto const score = organize::scoreProfiles(image, mate);
  CHECK(score.combined);
  CHECK(score.value == Catch::Approx(0.8 * 0.5 + 0.2 * 1.0));
  CHECK(organize::tauFor(score, organize::kCombinedTau) == organize::kCombinedTau);

  // No identity-tag evidence on either side: the feature cosine alone, judged
  // against the mode's own calibrated default rather than the knob.
  auto const untagged = organize::profileOf(
    item("c", analysis({tag("2girls", 0.95)}, {}, testutils::unitFeature(0.5)))
  );
  CHECK(untagged.tags.empty());
  auto const plain = organize::scoreProfiles(image, untagged);
  CHECK_FALSE(plain.combined);
  CHECK(plain.value == Catch::Approx(0.5));
  CHECK(organize::tauFor(plain, organize::kCombinedTau) == organize::kFeatureOnlyTau);
}

TEST_CASE(
  "clustering merges by mean cross-pair score, not by centroid distance",
  "[organize]"
) {
  // Three images 0/30/55 degrees apart, above the feature-only default: the
  // pair 0-30 (0.866) and the pair 30-55 (0.906) clear it, but a third image
  // joins them only if the *mean cross-pair* score does — 0 against 55 is
  // 0.574, so the mean of {30,55} against 0 is 0.72, below the default. A
  // centroid rule merges the trio instead, which is the dead end design D4
  // rejects; the same fixture therefore also pins that the partition does not
  // move when the pending order is reversed.
  auto const degrees = std::vector{0.0, 30.0, 55.0, 90.0};
  auto items = std::vector<organize::ImageItem>{};
  for (auto const index: {0, 1, 2, 3}) {
    auto const radians = degrees[index] * 3.14159265358979323846 / 180.0;
    items.push_back(item(
      std::format("chain-{}", index),
      analysis(
        {},
        {},
        std::vector<float>{
          static_cast<float>(std::cos(radians)),
          static_cast<float>(std::sin(radians)),
        }
      )
    ));
  }

  auto const partition = [](std::vector<organize::Cluster> const& clusters) {
    auto groups = std::vector<std::vector<std::size_t>>{};
    for (auto const& cluster: clusters) {
      auto group = cluster.itemIndices;
      std::sort(group.begin(), group.end());
      groups.push_back(std::move(group));
    }
    std::sort(groups.begin(), groups.end());
    return groups;
  };

  auto const forward = partition(organize::clusterPending(items, {0, 1, 2, 3}).clusters);
  auto const reversed = partition(organize::clusterPending(items, {3, 2, 1, 0}).clusters);
  CHECK(forward == reversed);
  // 30 and 55 are the only pair above the default; 0 and 90 stay alone.
  CHECK(forward == std::vector<std::vector<std::size_t>>{{0}, {1, 2}, {3}});
}

TEST_CASE("an item without an identity feature never joins a cluster", "[organize]") {
  // A failed analysis (or an unavailable identity model) leaves the feature
  // empty; the item falls through to uncategorized instead of clustering.
  auto items = std::vector<organize::ImageItem>{
    item("a", analysis({tag("pink_hair", 0.9)})),
    item("b", analysis({tag("pink_hair", 0.9)}, {}, testutils::unitFeature(1.0))),
  };
  auto const clusters = organize::clusterPending(items, {0, 1}).clusters;
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
  auto const clusters = organize::clusterPending(items, {0, 1, 2}).clusters;
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
  auto const clusters = organize::clusterPending(items, {0, 1}).clusters;
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
  auto const clusters = organize::clusterPending(items, {0}).clusters;
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

TEST_CASE("a lowered identity threshold reaches the clustering queue", "[organize]") {
  // 0.8 * 0.55 + 0.2 * 1.0 = 0.64: below the calibrated default but above a knob
  // the user lowered, which used to be filtered out before the pair was queued.
  auto items = std::vector<organize::ImageItem>{
    item("low-a", analysis({{"pink_hair", 0.9}}, {}, testutils::unitFeature(1.0))),
    item("low-b", analysis({{"pink_hair", 0.9}}, {}, testutils::unitFeature(0.55))),
  };

  CHECK(organize::clusterPending(items, {0, 1}, 0.62).clusters.size() == 1);
  // The calibrated default keeps them apart.
  CHECK(organize::clusterPending(items, {0, 1}).clusters.size() == 2);
}
