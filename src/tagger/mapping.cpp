#include "tagger/mapping.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace tagger {

double sigmoidConfidence(double logit) {
  return 1.0 / (1.0 + std::exp(-logit));
}

auto mapOutputs(
  std::span<float const> scores,
  Vocabulary const& vocabulary,
  double confidenceFloor
) -> TaggerOutput {
  auto output = TaggerOutput{};
  auto const limit = std::min(scores.size(), vocabulary.size());
  for (auto index = std::size_t{0}; index < limit; ++index) {
    auto const confidence = sigmoidConfidence(static_cast<double>(scores[index]));
    if (confidence < confidenceFloor) { continue; }
    auto const& entry = vocabulary[index];
    switch (entry.category) {
      case TagCategory::General:
        output.general.push_back({entry.name, confidence});
        break;
      case TagCategory::Character:
        output.character.push_back({entry.name, confidence});
        break;
      case TagCategory::Rating: output.rating.push_back({entry.name, confidence}); break;
      default                 : break;
    }
  }
  return output;
}

auto toInputFloats(std::span<std::uint8_t const> rgbBytes) -> std::vector<float> {
  auto floats = std::vector<float>{};
  floats.reserve(rgbBytes.size());
  std::transform(
    rgbBytes.begin(),
    rgbBytes.end(),
    std::back_inserter(floats),
    [](std::uint8_t byte) { return static_cast<float>(byte); }
  );
  return floats;
}

auto toIdentityInput(std::span<std::uint8_t const> rgbBytes) -> std::vector<float> {
  // Interleaved RGB in, planar NCHW floats in 0..1 out — the model's own
  // ToTensor (design D8).
  auto const pixels = rgbBytes.size() / 3;
  auto floats = std::vector<float>(rgbBytes.size(), 0.0F);
  for (auto channel = std::size_t{0}; channel < 3; ++channel) {
    for (auto pixel = std::size_t{0}; pixel < pixels; ++pixel) {
      floats[channel * pixels + pixel] =
        static_cast<float>(rgbBytes[pixel * 3 + channel]) / 255.0F;
    }
  }
  return floats;
}

void normalizeFeature(std::vector<float>& feature) {
  auto sum = 0.0;
  for (auto const value: feature) {
    sum += static_cast<double>(value) * static_cast<double>(value);
  }
  if (sum <= 0.0) {
    feature.clear();
    return;
  }
  auto const norm = static_cast<float>(std::sqrt(sum));
  for (auto& value: feature) { value /= norm; }
}

}  // namespace tagger
