// Output mapping and input-tensor conversion (task 2.3); the session seam
// itself needs a real model and is covered by the [real-model] smoke.
#include "tagger/mapping.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>

namespace {

auto vocab() -> tagger::Vocabulary {
  return tagger::Vocabulary{
    {.name = "1girl", .category = tagger::TagCategory::General},
    {.name = "hatsune_miku", .category = tagger::TagCategory::Character},
    {.name = "artist_foo", .category = tagger::TagCategory::Artist},
    {.name = "rating_explicit", .category = tagger::TagCategory::Rating},
    {.name = "meta_tag", .category = tagger::TagCategory::Meta},
  };
}

}  // namespace

TEST_CASE("sigmoidConfidence maps logits", "[tagger]") {
  CHECK(tagger::sigmoidConfidence(0.0) == 0.5);
  CHECK(tagger::sigmoidConfidence(100.0) > 0.99);
  CHECK(tagger::sigmoidConfidence(-100.0) < 0.01);
}

TEST_CASE("mapOutputs filters categories and applies the floor", "[tagger]") {
  auto const scores = std::vector<float>{0.0f, 4.0f, 100.0f, -6.0f, 8.0f};
  auto const output = tagger::mapOutputs(scores, vocab(), 0.5);

  // sigmoid(0)=0.5 kept, artist(100) dropped by category, sigmoid(4)~0.982
  // kept, sigmoid(-6)~0.002 floored, sigmoid(8)~0.9997 dropped as meta.
  REQUIRE(output.general.size() == 1);
  CHECK(output.general[0].tag == "1girl");
  REQUIRE(output.character.size() == 1);
  CHECK(output.character[0].tag == "hatsune_miku");
  CHECK(output.character[0].confidence > 0.98);
  CHECK(output.rating.empty());
  CHECK(std::abs(output.general[0].confidence - 0.5) < 1e-9);
}

TEST_CASE("toInputFloats casts bytes without normalization", "[tagger]") {
  auto const bytes = std::vector<std::uint8_t>{0, 127, 255};
  auto const floats = tagger::toInputFloats(bytes);
  REQUIRE(floats.size() == 3);
  CHECK(floats[0] == 0.0F);
  CHECK(floats[1] == 127.0F);
  CHECK(floats[2] == 255.0F);
}
