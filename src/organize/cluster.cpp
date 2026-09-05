#include "organize/cluster.h"

#include "organize/assign.h"
#include "organize/naming.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace organize {

namespace {

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
  // Scale the minimum with the corpus so small runs keep their traits.
  auto const minDf = std::max<std::size_t>(1, corpus / 20);
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
