// Appearance-tag clustering for the single-subject remainder (tasks 3.2/3.3,
// design D4/D5): top-K sparse vectors, greedy agglomerative assignment to
// centroids, unknown_<tags> folder naming with collision suffixes.
#pragma once

#include "organize/organize_types.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace organize {

inline constexpr auto kTopKTags = std::size_t{20};
inline constexpr auto kClusterTau = 0.82;  // tuned during acceptance (6.2)
inline constexpr auto kFolderTau = 0.80;   // tuned during acceptance (6.2)
inline constexpr auto kUnknownPrefix = "unknown_";

// Sparse appearance vector: general tags at or above the threshold, capped
// to the top K by confidence, as tag -> confidence.
auto appearanceVector(AnalysisResult const& analysis, double minConfidence)
  -> std::map<std::string, double>;

auto cosineSimilarity(
  std::map<std::string, double> const& a,
  std::map<std::string, double> const& b
) -> double;

// NOLINTNEXTLINE(bugprone-exception-escape): std::map members allocate by design
struct Cluster {
  std::vector<std::size_t> itemIndices;    // into the scanned items
  std::map<std::string, double> centroid;  // running mean of L2-normalized
};

// Greedy clustering over `pending` indices in content-hash order: join the
// nearest centroid at or above kClusterTau, else open a new cluster.
auto clusterPending(
  std::vector<ImageItem> const& items,
  std::vector<std::size_t> const& pending,
  double minConfidence
) -> std::vector<Cluster>;

// unknown_<top-tags> name for the cluster; collisions get _2/_3... suffixes.
// `items` provides content hashes for the deterministic fallback seed.
auto clusterFolderName(
  Cluster const& cluster,
  std::vector<ImageItem> const& items,
  std::set<std::string>& used
) -> std::string;

}  // namespace organize
