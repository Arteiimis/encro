// Raw-logit to tag mapping shared by OnnxTagger and its unit tests.
#pragma once

#include "tagger/tagger_types.h"
#include "tagger/vocabulary.h"

#include <cstdint>
#include <span>
#include <vector>

namespace tagger {

double sigmoidConfidence(double logit);

// Map model output columns through the vocabulary; artist/meta rows are
// dropped, confidences are sigmoid-transformed and floored.
auto mapOutputs(
  std::span<float const> scores,
  Vocabulary const& vocabulary,
  double confidenceFloor
) -> TaggerOutput;

// wd input contract: raw RGB bytes as float32 0..255, no normalization.
auto toInputFloats(std::span<std::uint8_t const> rgbBytes) -> std::vector<float>;

// Identity input contract: interleaved rgb24 bytes as planar NCHW float32,
// scaled to 0..1 and then normalized per channel with the documented channel
// mean and standard deviation of the pinned
// ccip-caformer-24-randaug-pruned/model_feat.onnx (the constants and their
// upstream source are in mapping.cpp).
auto toIdentityInput(std::span<std::uint8_t const> rgbBytes) -> std::vector<float>;

// Scales `feature` to unit length in place — the identity similarity contract
// reads a cosine, so magnitude must carry no information. A feature with no
// direction (all zeros, which a degenerate model output can be) becomes empty:
// the contract is "unit feature or no evidence", and an empty feature keeps
// such an image out of every cluster instead of giving it a singleton.
void normalizeFeature(std::vector<float>& feature);

}  // namespace tagger
