#include "organize/assign.h"

#include <algorithm>

namespace organize {

auto confidentCharacterTags(AnalysisResult const& analysis, double minConfidence)
  -> std::vector<TagScore> {
  auto tags = std::vector<TagScore>{};
  tags.reserve(analysis.character.size());
  std::copy_if(
    analysis.character.begin(),
    analysis.character.end(),
    std::back_inserter(tags),
    [&](TagScore const& tag) { return tag.confidence >= minConfidence; }
  );
  std::sort(tags.begin(), tags.end(), [](TagScore const& a, TagScore const& b) {
    if (a.confidence != b.confidence) { return a.confidence > b.confidence; }
    return a.tag < b.tag;
  });
  return tags;
}

auto isMultiSubject(AnalysisResult const& analysis, double minConfidence) -> bool {
  for (auto const& tag: analysis.general) {
    if (tag.confidence < minConfidence) { continue; }
    auto const match = std::ranges::find(kSubjectCountTags, tag.tag);
    if (match != std::ranges::end(kSubjectCountTags)) { return true; }
  }
  return false;
}

auto claimedTag(FolderReference const& reference) -> std::string {
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
