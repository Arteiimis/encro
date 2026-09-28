// Identity clustering for the single-subject remainder (design D4/D5): the
// identity model's feature vectors, one cosine threshold for both clustering
// and folder matching, greedy assignment to centroids, unknown_<tags> naming
// with collision suffixes.
#pragma once

#include "organize/organize_types.h"

#include <cstddef>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace organize {

// "Same character" acceptance: the cosine equivalent of the identity model's
// published metric threshold. Its shipped score is 0.5 * (1 - cosine) with the
// threshold at 0.178475, so the pipeline compares cosines against
// 1 - 2 * 0.178475 rather than carrying a second model file for the same
// arithmetic (design D3). One constant serves clustering and teaching because
// both ask the same question (design D4); the model's calibration is the only
// measured operating point. The best candidate at or above it wins, and the
// first best wins a tie, so each caller's own ordering decides equal scores.
inline constexpr auto kIdentityTau = 0.643050;
inline constexpr auto kUnknownPrefix = "unknown_";
// Confidence floor for a general tag to name a cluster: what the cache keeps
// (design D6) and the only thing naming reads.
inline constexpr auto kNamingConfidenceFloor = 0.55;

// Cosine similarity between two identity features. Both sides are normalized
// here — a centroid is a mean of unit features and is not unit itself — and a
// mismatch in length or an all-zero side reads as no similarity (0).
double cosineSimilarity(std::span<float const> a, std::span<float const> b);

// Folds one unit feature into a running mean of `count` members, sizing the
// mean for the first one; `count` advances with each fold. A cluster centroid
// and a taught folder's reference are the same shape, so the arithmetic lives
// once.
void accumulateFeature(
  std::vector<float>& mean,
  std::size_t& count,
  std::span<float const> feature
);

// NOLINTNEXTLINE(bugprone-exception-escape): vector members allocate by design
struct Cluster {
  std::vector<std::size_t> itemIndices;  // into the scanned items
  std::vector<float> centroid;           // running mean of unit features
};

// Greedy clustering over `pending` indices in content-hash order: join the
// best centroid at or above kIdentityTau, else open a new cluster. Items
// without a feature (analysis or extraction failed) never join one: they carry
// no identity evidence, and the caller sends them to uncategorized/.
auto clusterPending(
  std::vector<ImageItem> const& items,
  std::vector<std::size_t> const& pending
) -> std::vector<Cluster>;

// unknown_<top-tags> name for the cluster: the identity-bearing tags its
// members carry most often, sanitized and length-capped, with deterministic
// collision suffixes. The embedding carries no labels, so the name can only
// come from the tags; `items` also provides content hashes for the
// deterministic fallback seed when no member carries an identity tag.
auto clusterFolderName(
  Cluster const& cluster,
  std::vector<ImageItem> const& items,
  std::set<std::string>& used
) -> std::string;

}  // namespace organize
