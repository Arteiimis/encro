#include "organize/cluster.h"

#include "organize/assign.h"
#include "organize/naming.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string_view>
#include <utility>

namespace organize {

namespace {

// Identity-bearing tag substrings: the visual features that make a character
// recognizable (hair, eyes, anatomy, signature accessories). Acceptance on a
// 1899-image illustration dump showed unrestricted general-tag vectors are
// dominated by scene/action words and cannot tell characters apart (pairwise
// cosine p50 0.08); identity-only vectors cluster the same corpus cleanly.
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

bool isIdentityTag(std::string const& tag) {
  if (std::ranges::find(kIdentityBlocklist, tag) != kIdentityBlocklist.end()) {
    return false;
  }
  return std::ranges::any_of(kIdentityPatterns, [&](std::string_view pattern) {
    return tag.find(pattern) != std::string::npos;
  });
}

auto l2Norm(std::map<std::string, double> const& vector) -> double {
  auto sum = 0.0;
  for (auto const& [_, value]: vector) { sum += value * value; }
  return std::sqrt(sum);
}

}  // namespace

double cosineSimilarity(
  std::map<std::string, double> const& a,
  std::map<std::string, double> const& b
) {
  auto const normA = l2Norm(a);
  auto const normB = l2Norm(b);
  if (normA == 0.0 || normB == 0.0) { return 0.0; }
  auto dot = 0.0;
  for (auto const& [tag, value]: a) {
    if (auto const it = b.find(tag); it != b.end()) { dot += value * it->second; }
  }
  return dot / (normA * normB);
}

auto CorpusTraits::inTraitBand(std::string const& tag) const -> bool {
  // No corpus statistics (tests, single-image runs): no band filtering.
  if (df.empty()) { return true; }
  auto const it = df.find(tag);
  if (it == df.end()) { return false; }
  // Scale the minimum with the corpus (recurring-trait support ~2%), from an
  // absolute floor so small runs keep their traits, capped at a fifth of the
  // corpus so tiny runs keep theirs.
  auto const minDf = std::min(
    corpus / kTraitMinDfCorpusCapFraction,
    std::max(kTraitMinDfFloor, corpus / kTraitMinDfCorpusFraction)
  );
  return it->second >= minDf
    && static_cast<double>(it->second)
    <= kTraitMaxDfFraction * static_cast<double>(corpus);
}

auto buildCorpusTraits(std::vector<ImageItem> const& items) -> CorpusTraits {
  auto traits = CorpusTraits{};
  for (auto const& item: items) {
    if (!item.analysis.has_value()) { continue; }
    ++traits.corpus;
    auto seen = std::set<std::string>{};
    for (auto const& tag: item.analysis->general) {
      if (tag.confidence < kVectorFloor) { continue; }
      if (
        std::ranges::find(kSubjectCountTags, tag.tag)
        != std::ranges::end(kSubjectCountTags)
      ) {
        continue;
      }
      seen.insert(tag.tag);
    }
    for (auto const& tag: seen) { traits.df[tag] += 1; }
  }
  for (auto const& [tag, frequency]: traits.df) {
    traits.idf[tag] =
      std::log(static_cast<double>(traits.corpus) / static_cast<double>(frequency));
  }
  return traits;
}

auto appearanceVector(AnalysisResult const& analysis, CorpusTraits const& traits)
  -> std::map<std::string, double> {
  auto weighted = std::vector<TagScore>{};
  weighted.reserve(analysis.general.size());
  for (auto const& tag: analysis.general) {
    if (tag.confidence < kVectorFloor) { continue; }
    // Count tags route multi-subject images; they are not appearance.
    if (
      std::ranges::find(kSubjectCountTags, tag.tag) != std::ranges::end(kSubjectCountTags)
    ) {
      continue;
    }
    // Outside the trait band: collection constants and one-off scene noise
    // are equally useless for telling characters apart.
    if (!traits.inTraitBand(tag.tag)) { continue; }
    // Scene and action words describe the picture, not the person.
    if (!isIdentityTag(tag.tag)) { continue; }
    auto const weight =
      tag.confidence * (traits.idf.contains(tag.tag) ? traits.idf.at(tag.tag) : 1.0);
    weighted.push_back(TagScore{.tag = tag.tag, .confidence = weight});
  }
  std::sort(weighted.begin(), weighted.end(), [](TagScore const& a, TagScore const& b) {
    if (a.confidence != b.confidence) { return a.confidence > b.confidence; }
    return a.tag < b.tag;
  });

  auto vector = std::map<std::string, double>{};
  for (auto const& tag: weighted) {
    if (vector.size() >= kTopKTags) { break; }
    vector[tag.tag] = tag.confidence;
  }
  return vector;
}

auto normalizedAppearanceVector(
  AnalysisResult const& analysis,
  CorpusTraits const& traits
) -> std::map<std::string, double> {
  auto vector = appearanceVector(analysis, traits);
  auto const norm = l2Norm(vector);
  if (norm > 0.0) {
    for (auto& [_, value]: vector) { value /= norm; }
  }
  return vector;
}

auto clusterPending(
  std::vector<ImageItem> const& items,
  std::vector<std::size_t> const& pending,
  CorpusTraits const& traits
) -> std::vector<Cluster> {
  // Deterministic order: content-hash sorted indices.
  auto ordered = pending;
  std::sort(ordered.begin(), ordered.end(), [&](std::size_t a, std::size_t b) {
    return items[a].contentHash < items[b].contentHash;
  });

  auto clusters = std::vector<Cluster>{};
  for (auto const index: ordered) {
    auto const& analysis = items[index].analysis;
    if (!analysis.has_value()) { continue; }
    auto const normalized = normalizedAppearanceVector(*analysis, traits);
    if (normalized.empty()) { continue; }

    auto bestCluster = static_cast<Cluster*>(nullptr);
    auto bestScore = 0.0;
    for (auto& cluster: clusters) {
      auto const score = cosineSimilarity(normalized, cluster.centroid);
      if (score >= kClusterTau && score > bestScore) {
        bestCluster = &cluster;
        bestScore = score;
      }
    }

    auto& target = bestCluster != nullptr ? *bestCluster : clusters.emplace_back();
    target.itemIndices.push_back(index);

    // Running mean: n members before this one.
    auto const previous = static_cast<double>(target.itemIndices.size() - 1);
    for (auto& [_, value]: target.centroid) { value *= previous; }
    for (auto const& [tag, value]: normalized) { target.centroid[tag] += value; }
    auto const total = static_cast<double>(target.itemIndices.size());
    for (auto& [_, value]: target.centroid) { value /= total; }
  }
  return clusters;
}

auto clusterFolderName(
  Cluster const& cluster,
  std::vector<ImageItem> const& items,
  std::set<std::string>& used
) -> std::string {
  auto ranked = std::vector<std::pair<std::string, double>>{
    cluster.centroid.begin(),
    cluster.centroid.end()
  };
  std::sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) {
    if (a.second != b.second) { return a.second > b.second; }
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
    // Indistinguishable vectors: deterministic content fallback.
    auto seed = std::string{};
    for (auto const index: cluster.itemIndices) {
      seed += items[index].contentHash + "|";
    }
    sanitized = fallbackCharacterName(seed);
  }
  return assignUniqueFolderName(kUnknownPrefix + sanitized, used).name;
}

}  // namespace organize
