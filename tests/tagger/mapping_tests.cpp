// Output mapping and input-tensor conversion (task 2.3); the session seam
// itself needs a real model and is covered by the [real-model] smoke.
#include "tagger/mapping.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

// Mirrors the real wd-v3 vocabulary categories (0 general, 4 character,
// 9 rating).
auto vocab() -> tagger::Vocabulary {
  return tagger::Vocabulary{
    {.name = "1girl", .category = tagger::TagCategory::General},
    {.name = "hatsune_miku", .category = tagger::TagCategory::Character},
    {.name = "zzz_unknown_category", .category = static_cast<tagger::TagCategory>(1)},
    {.name = "rating_explicit", .category = tagger::TagCategory::Rating},
    {.name = "thighhighs", .category = tagger::TagCategory::General},
  };
}

}  // namespace

TEST_CASE("sigmoidConfidence maps logits", "[tagger]") {
  CHECK(tagger::sigmoidConfidence(0.0) == 0.5);
  CHECK(tagger::sigmoidConfidence(100.0) > 0.99);
  CHECK(tagger::sigmoidConfidence(-100.0) < 0.01);
}

TEST_CASE(
  "toIdentityInput lays the rgb frame out as normalized planar floats",
  "[tagger]"
) {
  // Two pixels: (255,0,0) and (0,128,255). The identity model's reference
  // preprocessing scales to 0..1 and then normalizes each channel with the
  // CLIP constants, so channels come out as planes, not interleaved, and the
  // values are no longer confined to 0..1.
  auto const rgb = std::vector<std::uint8_t>{255, 0, 0, 0, 128, 255};
  auto const floats = tagger::toIdentityInput(rgb);
  REQUIRE(floats.size() == 6);
  auto const normalized = [](double value, double mean, double deviation) {
    return (value - mean) / deviation;
  };
  CHECK(floats[0] == Catch::Approx(normalized(1.0, 0.48145466, 0.26862954)));  // R plane
  CHECK(floats[1] == Catch::Approx(normalized(0.0, 0.48145466, 0.26862954)));
  CHECK(floats[2] == Catch::Approx(normalized(0.0, 0.4578275, 0.26130258)));   // G plane
  CHECK(floats[3] == Catch::Approx(normalized(128.0 / 255.0, 0.4578275, 0.26130258)));
  CHECK(floats[4] == Catch::Approx(normalized(0.0, 0.40821073, 0.27577711)));  // B plane
  CHECK(floats[5] == Catch::Approx(normalized(1.0, 0.40821073, 0.27577711)));
}

TEST_CASE("normalizeFeature scales to unit length and leaves zero alone", "[tagger]") {
  auto feature = std::vector<float>{3.0F, 4.0F};
  tagger::normalizeFeature(feature);
  CHECK(feature[0] == Catch::Approx(0.6F));
  CHECK(feature[1] == Catch::Approx(0.8F));

  // No direction is no evidence: the feature becomes empty rather than a
  // vector every image is equidistant from.
  auto zero = std::vector<float>{0.0F, 0.0F};
  tagger::normalizeFeature(zero);
  CHECK(zero.empty());

  auto empty = std::vector<float>{};
  tagger::normalizeFeature(empty);
  CHECK(empty.empty());
}

TEST_CASE("mapOutputs filters categories and applies the floor", "[tagger]") {
  auto const scores = std::vector<float>{0.0f, 4.0f, 100.0f, -6.0f, 8.0f};
  auto const output = tagger::mapOutputs(scores, vocab(), 0.5);

  // sigmoid(0)=0.5 kept, unknown-category(100) dropped, sigmoid(4)~0.982
  // floored, sigmoid(8)~0.9997 kept as the second general tag.
  REQUIRE(output.general.size() == 2);
  CHECK(output.general[0].tag == "1girl");
  CHECK(output.general[1].tag == "thighhighs");
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
