// Routing stage (spec fixed order): exactly one confident character tag
// files by tag (redirected by folder ownership), multi-subject images go to
// mixed/, the single-subject remainder goes to clustering (tasks 3.1/3.4).
#pragma once

#include "organize/organize_types.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace organize {

// Design-constant subject-count tags feeding the multi-subject decision.
inline constexpr auto kSubjectCountTags = std::array{
  std::string_view{"2girls"},
  std::string_view{"3girls"},
  std::string_view{"4girls"},
  std::string_view{"5girls"},
  std::string_view{"6+girls"},
  std::string_view{"multiple_girls"},
  std::string_view{"2boys"},
  std::string_view{"3boys"},
  std::string_view{"4boys"},
  std::string_view{"5boys"},
  std::string_view{"6+boys"},
  std::string_view{"multiple_boys"},
};

inline constexpr auto kMixedFolder = "mixed";

// Character identities need far stronger evidence than appearance tags: the
// character head emits ~sigmoid(0)=0.5 for every unused identity, so the
// --min-confidence floor (0.35) would make all 2.7k identities candidates
// and route everything to mixed/. Community practice for wd taggers is a
// high character threshold (0.85). Tunable during acceptance.
inline constexpr auto kCharacterConfidence = 0.85;

// Character-tag candidates at or above kCharacterConfidence,
// confidence-descending.
auto confidentCharacterTags(AnalysisResult const& analysis) -> std::vector<TagScore>;

// True when a subject-count tag (general category) is present.
auto isMultiSubject(AnalysisResult const& analysis, double minConfidence) -> bool;

// A teaching reference folder: what its analyzable contents say it owns.
// NOLINTNEXTLINE(bugprone-exception-escape): std::map members allocate by design
struct FolderReference {
  fs::path name;  // current name on disk; a path so CJK survives
  std::map<std::string, double> meanVector;  // mean appearance vector
  std::size_t vectorMembers = 0;
  std::map<std::string, std::size_t>
    soleTagCounts;  // tag -> members whose sole candidate
  std::size_t analyzableMembers = 0;
};

// The tag this folder claims (sole at-or-above-threshold candidate for a
// majority of analyzable members), or "" when none. The tally must be built
// with the caller's current threshold.
auto claimedTag(FolderReference const& reference) -> std::string;

// The folder claiming `tag` with the most tagged members, or nullptr.
auto owningFolder(std::vector<FolderReference> const& references, std::string const& tag)
  -> FolderReference const*;

}  // namespace organize
