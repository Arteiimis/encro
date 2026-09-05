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
inline constexpr auto kClusterTau = 0.82;   // tuned during acceptance (6.2)
inline constexpr auto kFolderTau = 0.80;    // tuned during acceptance (6.2)
inline constexpr auto kUnknownPrefix = "unknown_";
inline constexpr auto kVectorFloor = 0.55;  // above the 0.5 noise band; see CorpusTraits
inline constexpr auto kTraitMaxDfFraction = 0.75;

// Corpus statistics shaping appearance vectors: idf weights suppress
// collection-constant tags (genre/censoring/framing fire on every image and
// describe the collection, not the character), and the df band keeps only
// mid-frequency traits — one-off scene tags and collection constants are
// equally useless for telling characters apart. Vector confidence floor is
// kVectorFloor (0.55): the 0.5 zero-evidence band extends to ~0.52 of noise
// and --min-confidence (0.35) lets the whole 8k-tag vocabulary through,
// which saturates df and empties vectors (both observed in acceptance).
// NOLINTNEXTLINE(bugprone-exception-escape): std::map members allocate by design
struct CorpusTraits {
  std::map<std::string, double> idf;
  std::map<std::string, std::size_t> df;
  std::size_t corpus = 0;

  auto inTraitBand(std::string const& tag) const -> bool;
};

// Builds corpus traits from every analyzed item's general tags (count tags
// excluded). Pass the result to the vector builders.
auto buildCorpusTraits(std::vector<ImageItem> const& items) -> CorpusTraits;

// Sparse appearance vector: general tags in the trait band, capped to the
// top K by idf-weighted confidence, as tag -> weight.
auto appearanceVector(AnalysisResult const& analysis, CorpusTraits const& traits = {})
  -> std::map<std::string, double>;

// appearanceVector with the result L2-normalized (similarity-ready).
auto normalizedAppearanceVector(
  AnalysisResult const& analysis,
  CorpusTraits const& traits = {}
) -> std::map<std::string, double>;

double cosineSimilarity(
  std::map<std::string, double> const& a,
  std::map<std::string, double> const& b
);

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
  CorpusTraits const& traits = {}
) -> std::vector<Cluster>;

// unknown_<top-tags> name for the cluster; collisions get _2/_3... suffixes.
// `items` provides content hashes for the deterministic fallback seed.
auto clusterFolderName(
  Cluster const& cluster,
  std::vector<ImageItem> const& items,
  std::set<std::string>& used
) -> std::string;

}  // namespace organize
