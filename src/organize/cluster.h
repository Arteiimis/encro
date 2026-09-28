// Identity clustering for the single-subject remainder: a combined score over
// the identity feature and the image's own identity-bearing tags, average
// linkage over the pairwise scores, unknown_<tags> naming with collision
// suffixes.
#pragma once

#include "organize/organize_types.h"

#include <cstddef>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace organize {

// Weights of the combined score (design D2): the identity feature is the
// primary signal, the tag side only breaks its near-ties.
inline constexpr auto kFeatureWeight = 0.8;
inline constexpr auto kTagWeight = 0.2;

// Both calibrated defaults (kCombinedTau, kFeatureOnlyTau) and the ceiling live
// in organize_types.h beside Options, because a run's threshold is
// configuration: the CLI help and the config surface read the same values.
inline constexpr auto kUnknownPrefix = "unknown_";
// Confidence floor for a general tag to name a cluster and to count as tag
// evidence: what the cache keeps (design D6).
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

// The tag side of the score is TagVector (organize_types.h): sparse over the
// identity-bearing general tags at or above the naming floor, weighted by
// confidence and with no corpus statistic (design D1).
auto identityTagVector(std::vector<TagScore> const& generalTags) -> TagVector;
double tagCosine(TagVector const& a, TagVector const& b);

// Folds one sparse unit vector into a running mean of `count` members, sizing
// the mean for the first one; `count` advances with each fold.
void accumulateTagVector(TagVector& mean, std::size_t& count, TagVector const& vector);

// One image's two sides of the score.
struct
  IdentityProfile {  // NOLINT(bugprone-exception-escape): standard-container members only
  std::vector<float> feature;  // unit; empty when the image carries no evidence
  TagVector tags;              // unit; empty when no identity tag reaches the floor
};

// The profile of one analysed image.
auto profileOf(ImageItem const& item) -> IdentityProfile;

// The comparison of two profiles: the 0.8/0.2 weighted mean of the feature
// cosine and the tag cosine when both sides carry tag evidence, the feature
// cosine alone otherwise (design D3). `combined` reports which mode the value
// is in, because each mode has its own calibrated default.
struct ProfileScore {
  double value = 0.0;
  bool combined = false;
};
auto scoreProfiles(IdentityProfile const& a, IdentityProfile const& b) -> ProfileScore;

// The threshold a score is judged against: the caller's knob for a combined
// score, the feature-only default when a side carried no tag evidence.
double tauFor(ProfileScore const& score, double combinedTau);

// NOLINTNEXTLINE(bugprone-exception-escape): vector members allocate by design
struct Cluster {
  std::vector<std::size_t> itemIndices;  // into the scanned items
  std::vector<float> centroid;           // mean of the members' unit features
  TagVector meanTags;                    // mean of the members' unit tag vectors
  std::size_t tagMembers = 0;            // members that contributed a tag vector
};

// What the clustering produced, and whether it had to take the low-memory path.
struct ClusterResult {
  std::vector<Cluster> clusters;
  bool fellBack = false;
};

// Average-linkage clustering over `pending` indices (design D4): repeatedly
// merge the pair of clusters whose mean cross-pair score is highest, while that
// score reaches its own mode's threshold, breaking ties by cluster creation
// order so the same input always produces the same partition. Above `ceiling`
// analysed images it runs the previous greedy pass instead and reports that
// (design D5). Items without a feature never join a cluster: they carry no
// identity evidence, and the caller sends them to uncategorized/.
auto clusterPending(
  std::vector<ImageItem> const& items,
  std::vector<std::size_t> const& pending,
  double combinedTau = kCombinedTau,
  std::size_t ceiling = kAgglomerationImageCeiling
) -> ClusterResult;

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
