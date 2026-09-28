#include "organize/cluster.h"

#include "organize/naming.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <queue>
#include <ranges>
#include <string_view>
#include <utility>

namespace organize {

namespace {

// Identity-bearing tag substrings: the visual features that make a character
// recognizable (hair, eyes, anatomy, signature accessories). They name
// clusters — the identity model has no vocabulary — and they are the tag side
// of the combined score.
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

// Whether a tag counts as evidence of who the character is: it must clear the
// floor the cache keeps, and describe the person rather than the scene.
// Scoring and naming read this one rule so they cannot drift apart.
bool countsAsTagEvidence(std::string const& tag, double confidence) {
  return confidence >= kNamingConfidenceFloor && isIdentityTag(tag);
}

// Which mode a comparison is in: the weighted mean of the two cosines needs tag
// evidence on both sides, and a running mean reports it as a member count.
constexpr bool bothCarryTagEvidence(std::size_t left, std::size_t right) {
  return left > 0 && right > 0;
}

// Marks an item that carries no usable feature, so it never joins a cluster.
constexpr auto kNoProfile = std::numeric_limits<std::size_t>::max();

// The member-pair score sums of the running clusters, condensed to the upper
// triangle: for clusters A and B, the sum of the scores over every pair of
// their members. The merge rule reads the mean cross-pair score as
// sum / (|A| * |B|), and a merge updates the survivor's sums in O(k) — this
// matrix is the only O(n^2) structure the run allocates, which is what the
// image-count ceiling exists for (design D5).
class PairSums {
public:
  explicit PairSums(std::size_t size): size_(size), pairs_(size * (size - 1) / 2, 0.0) { }

  double get(std::size_t a, std::size_t b) const { return pairs_[index(a, b)]; }
  void set(std::size_t a, std::size_t b, double value) { pairs_[index(a, b)] = value; }

private:
  std::size_t index(std::size_t a, std::size_t b) const {
    if (a > b) { std::swap(a, b); }
    return a * size_ - a * (a + 1) / 2 + (b - a - 1);
  }

  std::size_t size_ = 0;
  std::vector<double> pairs_;
};

// A candidate merge with the cluster versions its score was computed at, so a
// stale entry is recognised when it is popped. Ordering is by score, then by
// the two cluster ids, which makes the choice of pair deterministic.
struct Candidate {
  double score = 0.0;
  std::size_t left = 0;
  std::size_t right = 0;
  std::size_t leftVersion = 0;
  std::size_t rightVersion = 0;
};

struct EarlierPair {
  bool operator()(Candidate const& a, Candidate const& b) const {
    if (a.score != b.score) { return a.score < b.score; }
    if (a.left != b.left) { return a.left > b.left; }
    return a.right > b.right;
  }
};

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

auto identityTagVector(std::vector<TagScore> const& generalTags) -> TagVector {
  auto strongest = std::map<std::string, double>{};
  for (auto const& tag: generalTags) {
    if (!countsAsTagEvidence(tag.tag, tag.confidence)) { continue; }
    auto& kept = strongest[tag.tag];
    kept = std::max(kept, tag.confidence);
  }

  auto squared = 0.0;
  for (auto const& entry: strongest) { squared += entry.second * entry.second; }
  if (squared <= 0.0) { return TagVector{}; }
  auto const norm = std::sqrt(squared);
  for (auto& entry: strongest) { entry.second /= norm; }
  return strongest;
}

double tagCosine(TagVector const& a, TagVector const& b) {
  if (a.empty() || b.empty()) { return 0.0; }
  auto dot = 0.0;
  auto normA = 0.0;
  auto normB = 0.0;
  for (auto const& [name, weight]: a) {
    normA += weight * weight;
    auto const other = b.find(name);
    if (other != b.end()) { dot += weight * other->second; }
  }
  for (auto const& entry: b) { normB += entry.second * entry.second; }
  if (normA <= 0.0 || normB <= 0.0) { return 0.0; }
  return dot / (std::sqrt(normA) * std::sqrt(normB));
}

void accumulateTagVector(TagVector& mean, std::size_t& count, TagVector const& vector) {
  auto const members = static_cast<double>(count);
  auto const total = members + 1.0;
  for (auto& [name, weight]: mean) { weight *= members / total; }
  for (auto const& [name, weight]: vector) { mean[name] += weight / total; }
  count += 1;
}

auto profileOf(ImageItem const& item) -> IdentityProfile {
  auto profile = IdentityProfile{};
  if (!item.analysis.has_value()) { return profile; }
  profile.feature = item.analysis->identity;
  profile.tags = identityTagVector(item.analysis->tags.general);
  return profile;
}

auto scoreProfiles(IdentityProfile const& a, IdentityProfile const& b) -> ProfileScore {
  auto const feature = cosineSimilarity(a.feature, b.feature);
  if (!bothCarryTagEvidence(a.tags.size(), b.tags.size())) {
    return ProfileScore{.value = feature, .combined = false};
  }
  return ProfileScore{
    .value = kFeatureWeight * feature + kTagWeight * tagCosine(a.tags, b.tags),
    .combined = true,
  };
}

double tauFor(ProfileScore const& score, double combinedTau) {
  return score.combined ? combinedTau : kFeatureOnlyTau;
}

// Low-memory path (design D5): the previous greedy pass over the same combined
// score, so a gallery above the ceiling degrades instead of failing.
auto greedyGroups(
  std::vector<IdentityProfile> const& profiles,
  std::vector<std::size_t> const& indices,
  double combinedTau
) -> std::vector<std::vector<std::size_t>> {
  auto clusters = std::vector<Cluster>{};
  for (auto const local: std::views::iota(std::size_t{0}, profiles.size())) {
    auto best = static_cast<Cluster*>(nullptr);
    auto bestScore = 0.0;
    for (auto& cluster: clusters) {
      auto const score = scoreProfiles(
        profiles[local],
        IdentityProfile{.feature = cluster.centroid, .tags = cluster.meanTags}
      );
      if (score.value < tauFor(score, combinedTau)) { continue; }
      if (best != nullptr && score.value <= bestScore) { continue; }
      best = &cluster;
      bestScore = score.value;
    }
    auto& target = best != nullptr ? *best : clusters.emplace_back();
    auto members = target.itemIndices.size();
    accumulateFeature(target.centroid, members, profiles[local].feature);
    if (!profiles[local].tags.empty()) {
      accumulateTagVector(target.meanTags, target.tagMembers, profiles[local].tags);
    }
    target.itemIndices.push_back(indices[local]);
  }
  auto groups = std::vector<std::vector<std::size_t>>{};
  for (auto& cluster: clusters) { groups.push_back(std::move(cluster.itemIndices)); }
  return groups;
}

// Agglomeration bookkeeping, together so each merge step stays a small
// function: condensed pair sums, live clusters and the candidate queue.
struct MergeState {
  PairSums sums;
  std::vector<std::vector<std::size_t>> members;
  std::vector<std::size_t> counts;
  std::vector<std::size_t> tagMembers;
  std::vector<std::size_t> versions;
  std::vector<bool> alive;
  std::priority_queue<Candidate, std::vector<Candidate>, EarlierPair> candidates;

  explicit MergeState(std::size_t size)
    : sums(size),
      members(size),
      counts(size, 1),
      tagMembers(size, 0),
      versions(size, 0),
      alive(size, true) { }
};

// Both defaults sit at or above this floor, so a pair below it can never pass in
// either mode and never needs queueing. The floor is the lower of the two
// thresholds actually in play, so a lowered --identity-tau still merges.
constexpr bool passesLowerDefault(ProfileScore const& score, double floor) {
  return score.value >= floor;
}

void seedCandidates(
  MergeState& state,
  std::vector<IdentityProfile> const& profiles,
  double floor
) {
  auto const size = profiles.size();
  for (auto left = std::size_t{0}; left < size; ++left) {
    state.members[left].push_back(left);
    state.tagMembers[left] = profiles[left].tags.empty() ? 0 : 1;
    for (auto right = left + 1; right < size; ++right) {
      auto const score = scoreProfiles(profiles[left], profiles[right]);
      state.sums.set(left, right, score.value);
      if (!passesLowerDefault(score, floor)) { continue; }
      state.candidates.push(
        Candidate{
          .score = score.value,
          .left = left,
          .right = right,
          .leftVersion = state.versions[left],
          .rightVersion = state.versions[right],
        }
      );
    }
  }
}

// Folds `absorbed` into `survivor` and requeues the survivor's pairs: the
// condensed sums of the two old clusters add up, so this costs O(others).
void absorbCluster(
  MergeState& state,
  std::size_t survivor,
  std::size_t absorbed,
  double floor
) {
  auto const size = state.members.size();
  for (auto const member: state.members[absorbed]) {
    state.members[survivor].push_back(member);
  }
  state.members[absorbed].clear();
  state.counts[survivor] += state.counts[absorbed];
  state.tagMembers[survivor] += state.tagMembers[absorbed];
  state.tagMembers[absorbed] = 0;
  state.alive[absorbed] = false;
  state.versions[survivor] += 1;

  for (auto other = std::size_t{0}; other < size; ++other) {
    if (!state.alive[other] || other == survivor) { continue; }
    auto const added = state.sums.get(survivor, other) + state.sums.get(absorbed, other);
    auto const low = std::min(survivor, other);
    auto const high = std::max(survivor, other);
    state.sums.set(low, high, added);
    auto const updated = ProfileScore{
      .value = added / static_cast<double>(state.counts[low] * state.counts[high]),
      .combined = bothCarryTagEvidence(state.tagMembers[low], state.tagMembers[high]),
    };
    if (!passesLowerDefault(updated, floor)) { continue; }
    state.candidates.push(
      Candidate{
        .score = updated.value,
        .left = low,
        .right = high,
        .leftVersion = state.versions[low],
        .rightVersion = state.versions[high],
      }
    );
  }
}

// Average linkage (design D4): merge the pair of clusters whose mean cross-pair
// score is highest while it reaches its mode's threshold.
auto agglomerateGroups(
  std::vector<IdentityProfile> const& profiles,
  std::vector<std::size_t> const& indices,
  double combinedTau
) -> std::vector<std::vector<std::size_t>> {
  auto const size = profiles.size();
  auto state = MergeState{size};
  // The floor below which no mode can merge: a lowered knob has to reach the
  // queue, not only the per-mode test.
  auto const floor = std::min(combinedTau, kFeatureOnlyTau);
  seedCandidates(state, profiles, floor);

  while (!state.candidates.empty()) {
    auto const top = state.candidates.top();
    state.candidates.pop();
    if (!state.alive[top.left] || !state.alive[top.right]) { continue; }
    if (
      state.versions[top.left] != top.leftVersion
      || state.versions[top.right] != top.rightVersion
    ) {
      continue;
    }

    // The versions above prove neither cluster changed since this score was
    // computed, so the queued value is still the mean cross-pair score.
    auto const score = top.score;
    // Nothing left can pass: the floor is the lower of the two mode thresholds.
    if (score < floor) { break; }
    auto const mode = ProfileScore{
      .value = score,
      .combined =
        bothCarryTagEvidence(state.tagMembers[top.left], state.tagMembers[top.right]),
    };
    if (score < tauFor(mode, combinedTau)) { continue; }

    // Deterministic survivor: the earlier-created cluster keeps its id.
    absorbCluster(
      state,
      std::min(top.left, top.right),
      std::max(top.left, top.right),
      floor
    );
  }

  auto groups = std::vector<std::vector<std::size_t>>{};
  for (auto local = std::size_t{0}; local < size; ++local) {
    if (!state.alive[local]) { continue; }
    auto group = std::vector<std::size_t>{};
    for (auto const member: state.members[local]) { group.push_back(indices[member]); }
    groups.push_back(std::move(group));
  }
  return groups;
}

auto clusterPending(
  std::vector<ImageItem> const& items,
  std::vector<std::size_t> const& pending,
  double combinedTau,
  std::size_t ceiling
) -> ClusterResult {
  // Deterministic order: content-hash sorted indices.
  auto ordered = pending;
  std::sort(ordered.begin(), ordered.end(), [&](std::size_t a, std::size_t b) {
    return items[a].contentHash < items[b].contentHash;
  });

  auto indices = std::vector<std::size_t>{};
  auto profiles = std::vector<IdentityProfile>{};
  // Item index -> profile index, so the cluster means below reuse the profiles
  // instead of deriving them a second time.
  auto profileOfItem = std::vector<std::size_t>(items.size(), kNoProfile);
  for (auto const index: ordered) {
    auto const& analysis = items[index].analysis;
    if (!analysis.has_value() || analysis->identity.empty()) { continue; }
    indices.push_back(index);
    profileOfItem[index] = profiles.size();
    profiles.push_back(profileOf(items[index]));
  }

  auto const size = profiles.size();
  auto groups = std::vector<std::vector<std::size_t>>{};
  auto fellBack = false;

  if (size > ceiling) {
    fellBack = true;
    groups = greedyGroups(profiles, indices, combinedTau);
  } else {
    groups = agglomerateGroups(profiles, indices, combinedTau);
  }

  auto result = ClusterResult{.clusters = {}, .fellBack = fellBack};
  for (auto& group: groups) {
    auto cluster = Cluster{.itemIndices = std::move(group)};
    auto featureMembers = std::size_t{0};
    for (auto const index: cluster.itemIndices) {
      auto const local = profileOfItem[index];
      if (local == kNoProfile) { continue; }
      accumulateFeature(cluster.centroid, featureMembers, profiles[local].feature);
      if (!profiles[local].tags.empty()) {
        accumulateTagVector(cluster.meanTags, cluster.tagMembers, profiles[local].tags);
      }
    }
    result.clusters.push_back(std::move(cluster));
  }
  return result;
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
      if (!countsAsTagEvidence(tag.tag, tag.confidence)) { continue; }
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
