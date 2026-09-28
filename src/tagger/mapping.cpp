#include "tagger/mapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

namespace tagger {

namespace {

// The identity model's channel normalization, from the reference
// implementation's `imgutils/metrics/ccip.py` `_normalize`: the CLIP constants,
// applied after scaling to 0..1. The contract and the pinned model file are in
// mapping.h.
constexpr std::size_t kChannels = 3;
constexpr std::array<float, kChannels> kChannelMean{0.48145466F, 0.4578275F, 0.40821073F};
constexpr std::array<float, kChannels> kChannelStd{0.26862954F, 0.26130258F, 0.27577711F};

}  // namespace

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
  // Interleaved rgb24 in, planar NCHW out.
  auto const pixels = rgbBytes.size() / kChannels;
  auto floats = std::vector<float>(rgbBytes.size(), 0.0F);
  for (auto channel = std::size_t{0}; channel < kChannels; ++channel) {
    for (auto pixel = std::size_t{0}; pixel < pixels; ++pixel) {
      auto const scaled =
        static_cast<float>(rgbBytes[pixel * kChannels + channel]) / 255.0F;
      floats[channel * pixels + pixel] =
        (scaled - kChannelMean[channel]) / kChannelStd[channel];
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
