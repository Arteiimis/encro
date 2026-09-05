#include "tagger/mapping.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace tagger {

auto sigmoidConfidence(double logit) -> double {
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

}  // namespace tagger
