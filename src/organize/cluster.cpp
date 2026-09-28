#include "organize/cluster.h"

#include "organize/naming.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <string_view>
#include <utility>

namespace organize {

namespace {

// Identity-bearing tag substrings: the visual features that make a character
// recognizable (hair, eyes, anatomy, signature accessories). They name
// clusters — the identity model has no vocabulary — and are the one survivor
// of the tag-vector appearance signal the embedding replaced.
constexpr auto kIdentityPatterns = std::array{
  std::string_view{"hair"},     std::string_view{"eyes"},
  std::string_view{"ahoge"},    std::string_view{"bangs"},
  std::string_view{"ponytail"}, std::string_view{"twintails"},
  std::string_view{"braid"},    std::string_view{"sidelocks"},
  std::string_view{"horn"},     std::string_view{"tail"},
  std::string_view{"ears"},     std::string_view{"wing"},
  std::string_view{"glasses"},  std::string_view{"eyepatch"},
  std::string_view{"mask"},     std::string_view{"headband"},
  std::string_view{"hairband"}, std::string_view{"hair_ornament"},
  std::string_view{"earrings"}, std::string_view{"halo"},
  std::string_view{"antennae"}, std::string_view{"fangs"},
};

// Tags matching a pattern but describing the scene or an expression, not the
// person ("tears" contains "ears", "cocktail" contains "tail", ...).
constexpr auto kIdentityBlocklist = std::array{
  std::string_view{"pubic_hair"},
  std::string_view{"male_pubic_hair"},
  std::string_view{"female_pubic_hair"},
  std::string_view{"body_hair"},
  std::string_view{"armpit_hair"},
  std::string_view{"facial_hair"},
  std::string_view{"chest_hair"},
  std::string_view{"cum_on_hair"},
  std::string_view{"tears"},
  std::string_view{"horny"},
  std::string_view{"cocktail"},
  std::string_view{"closed_eyes"},
  std::string_view{"half-closed_eyes"},
  std::string_view{"almost-closed_eyes"},
  std::string_view{"empty_eyes"},
  std::string_view{"rolling_eyes"},
  std::string_view{"one_eye_closed"},
  std::string_view{"mask_remove"},
  std::string_view{"mask_removed"},
  std::string_view{"mask_off"},
  std::string_view{"mask_on"},
  std::string_view{"holding_mask"},
};

// How often a tag names a cluster, and how sure the members were of it.
struct TagTally {
  std::size_t members = 0;
  double confidenceTotal = 0.0;
};

bool isIdentityTag(std::string const& tag) {
  if (std::ranges::find(kIdentityBlocklist, tag) != kIdentityBlocklist.end()) {
    return false;
  }
  return std::ranges::any_of(kIdentityPatterns, [&](std::string_view pattern) {
    return tag.find(pattern) != std::string::npos;
  });
}

}  // namespace

double cosineSimilarity(std::span<float const> a, std::span<float const> b) {
  if (a.empty() || a.size() != b.size()) { return 0.0; }
  auto dot = 0.0;
  auto normA = 0.0;
  auto normB = 0.0;
  for (auto index = std::size_t{0}; index < a.size(); ++index) {
    auto const left = static_cast<double>(a[index]);
    auto const right = static_cast<double>(b[index]);
    dot += left * right;
    normA += left * left;
    normB += right * right;
  }
  if (normA <= 0.0 || normB <= 0.0) { return 0.0; }
  return dot / (std::sqrt(normA) * std::sqrt(normB));
}

void accumulateFeature(
  std::vector<float>& mean,
  std::size_t& count,
  std::span<float const> feature
) {
  auto const members = static_cast<double>(count);
  auto const total = members + 1.0;
  if (mean.size() != feature.size()) { mean.assign(feature.size(), 0.0F); }
  for (auto index = std::size_t{0}; index < feature.size(); ++index) {
    auto const accumulated = static_cast<double>(mean[index]) * members + feature[index];
    mean[index] = static_cast<float>(accumulated / total);
  }
  count += 1;
}

auto clusterPending(
  std::vector<ImageItem> const& items,
  std::vector<std::size_t> const& pending
) -> std::vector<Cluster> {
  // Deterministic order: content-hash sorted indices.
  auto ordered = pending;
  std::sort(ordered.begin(), ordered.end(), [&](std::size_t a, std::size_t b) {
    return items[a].contentHash < items[b].contentHash;
  });

  auto clusters = std::vector<Cluster>{};
  for (auto const index: ordered) {
    auto const& analysis = items[index].analysis;
    if (!analysis.has_value() || analysis->identity.empty()) { continue; }
    auto const& feature = analysis->identity;

    // The best centroid at or above the threshold (kIdentityTau says which
    // one wins a tie).
    auto bestCluster = static_cast<Cluster*>(nullptr);
    auto bestScore = kIdentityTau;
    for (auto& cluster: clusters) {
      if (cluster.centroid.size() != feature.size()) { continue; }
      auto const score = cosineSimilarity(feature, cluster.centroid);
      if (score < kIdentityTau) { continue; }
      if (bestCluster != nullptr && score <= bestScore) { continue; }
      bestCluster = &cluster;
      bestScore = score;
    }

    auto& target = bestCluster != nullptr ? *bestCluster : clusters.emplace_back();
    auto members = target.itemIndices.size();
    accumulateFeature(target.centroid, members, feature);
    target.itemIndices.push_back(index);
  }
  return clusters;
}

auto clusterFolderName(
  Cluster const& cluster,
  std::vector<ImageItem> const& items,
  std::set<std::string>& used
) -> std::string {
  // Rank by how many members carry the tag: a cluster is described by what its
  // members share, not by the single loudest image (design D5).
  auto tally = std::map<std::string, TagTally>{};
  for (auto const index: cluster.itemIndices) {
    auto const& analysis = items[index].analysis;
    if (!analysis.has_value()) { continue; }
    for (auto const& tag: analysis->tags.general) {
      if (tag.confidence < kNamingConfidenceFloor || !isIdentityTag(tag.tag)) {
        continue;
      }
      auto& entry = tally[tag.tag];
      entry.members += 1;
      entry.confidenceTotal += tag.confidence;
    }
  }

  auto ranked = std::vector<std::pair<std::string, TagTally>>{tally.begin(), tally.end()};
  std::sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) {
    if (a.second.members != b.second.members) {
      return a.second.members > b.second.members;
    }
    // Equal member counts share the divisor, so the sums order the means.
    if (a.second.confidenceTotal != b.second.confidenceTotal) {
      return a.second.confidenceTotal > b.second.confidenceTotal;
    }
    return a.first < b.first;
  });

  auto joined = std::string{};
  for (
    auto index = std::size_t{0}; index < ranked.size() && index < std::size_t{3}; ++index
  ) {
    joined += (joined.empty() ? "" : "_") + ranked[index].first;
  }
  auto sanitized = sanitizeCharacterName(joined);
  if (sanitized.empty()) {
    // No member carries an identity tag: deterministic content fallback.
    auto seed = std::string{};
    for (auto const index: cluster.itemIndices) {
      seed += items[index].contentHash + "|";
    }
    sanitized = fallbackCharacterName(seed);
  }
  return assignUniqueFolderName(kUnknownPrefix + sanitized, used).name;
}

}  // namespace organize
