#include "organize/assign.h"

#include <algorithm>
#include <map>
#include <set>

namespace organize {

auto positiveCharacterTags(AnalysisResult const& analysis) -> std::vector<TagScore> {
  auto tags = std::vector<TagScore>{};
  tags.reserve(analysis.character.size());
  std::copy_if(
    analysis.character.begin(),
    analysis.character.end(),
    std::back_inserter(tags),
    [&](TagScore const& tag) { return tag.confidence > kZeroEvidence; }
  );
  std::sort(tags.begin(), tags.end(), [](TagScore const& a, TagScore const& b) {
    if (a.confidence != b.confidence) { return a.confidence > b.confidence; }
    return a.tag < b.tag;
  });
  return tags;
}

bool hasStrongCountTag(AnalysisResult const& analysis) {
  // Count tags are subject assertions: like character identities they need
  // strong evidence (the ~0.5 sigmoid band is zero-evidence noise).
  for (auto const& tag: analysis.general) {
    if (tag.confidence < kSubjectCountConfidence) { continue; }
    auto const match = std::ranges::find(kSubjectCountTags, tag.tag);
    if (match != std::ranges::end(kSubjectCountTags)) { return true; }
  }
  return false;
}

auto buildCharacterDf(std::vector<ImageItem> const& items)
  -> std::map<std::string, std::size_t> {
  auto df = std::map<std::string, std::size_t>{};
  for (auto const& item: items) {
    if (!item.analysis.has_value()) { continue; }
    auto seen = std::set<std::string>{};
    for (auto const& tag: item.analysis->character) {
      if (tag.confidence > kZeroEvidence) { seen.insert(tag.tag); }
    }
    for (auto const& tag: seen) { df[tag] += 1; }
  }
  return df;
}

auto isCredibleCandidate(
  TagScore const& candidate,
  std::map<std::string, std::size_t> const& characterDf
) -> bool {
  if (candidate.confidence >= kCharacterConfidence) { return true; }
  auto const it = characterDf.find(candidate.tag);
  return candidate.confidence >= kWeakConfidence
    && it != characterDf.end()
    && it->second >= kMinCharacterDf;
}

std::string claimedTag(FolderReference const& reference) {
  auto best = std::string{};
  auto bestCount = std::size_t{0};
  for (auto const& [tag, count]: reference.soleTagCounts) {
    if (count * 2 > reference.analyzableMembers && count > bestCount) {
      best = tag;
      bestCount = count;
    }
  }
  return best;
}

auto owningFolder(std::vector<FolderReference> const& references, std::string const& tag)
  -> FolderReference const* {
  auto const* best = static_cast<FolderReference const*>(nullptr);
  auto bestCount = std::size_t{0};
  for (auto const& reference: references) {
    if (claimedTag(reference) != tag) { continue; }
    auto const count = reference.soleTagCounts.at(tag);
    if (count > bestCount) {
      best = &reference;
      bestCount = count;
    }
  }
  return best;
}

}  // namespace organize
