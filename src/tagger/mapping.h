// Raw-logit to tag mapping shared by OnnxTagger and its unit tests.
#pragma once

#include "tagger/tagger_types.h"
#include "tagger/vocabulary.h"

#include <cstdint>
#include <span>
#include <vector>

namespace tagger {

auto sigmoidConfidence(double logit) -> double;

// Map model output columns through the vocabulary; artist/meta rows are
// dropped, confidences are sigmoid-transformed and floored.
auto mapOutputs(
  std::span<float const> scores,
  Vocabulary const& vocabulary,
  double confidenceFloor
) -> TaggerOutput;

// wd input contract: raw RGB bytes as float32 0..255, no normalization.
auto toInputFloats(std::span<std::uint8_t const> rgbBytes) -> std::vector<float>;

}  // namespace tagger
