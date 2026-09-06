// Routing stage (spec fixed order): exactly one confident character tag
// files by tag (redirected by folder ownership), multi-subject images go to
// mixed/, the single-subject remainder goes to clustering (tasks 3.1/3.4).
#pragma once

#include "organize/organize_types.h"

#include <array>
#include <map>
#include <string>
#include <string_view>
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
// and route everything to mixed/. AI-generated art sits off the training
// distribution and systematically depresses character confidence (real
// identities fire ~0.6-0.85 where Danbooru originals fire 0.9+), so the
// strong threshold sits at 0.60. Confidence above 0.5 (a zero logit) is
// weak-but-positive evidence and is enough to claim an image when it is the
// only candidate. Count-tag assertions (2boys, ...) use a strong 0.85
// threshold: they must never fire on the 0.5 noise band. All tunable during
// acceptance.
inline constexpr auto kCharacterConfidence = 0.60;
inline constexpr auto kZeroEvidence = 0.50;
inline constexpr auto kSubjectCountConfidence = 0.85;
// A character candidate below the strong threshold is credible when it fires
// at least kWeakConfidence on at least kMinCharacterDf images: consistent
// cross-image agreement separates a real identity from per-image noise
// (acceptance: noise candidates capped at 0.5213, the real identity spanned
// 0.53-0.73 across 32 of 83 images).
inline constexpr auto kWeakConfidence = 0.53;
inline constexpr auto kMinCharacterDf = std::size_t{3};

// Identity-bearing character candidates (confidence above the zero-evidence
// floor), confidence-descending. Strong candidates additionally satisfy
// confidence >= kCharacterConfidence.
auto positiveCharacterTags(AnalysisResult const& analysis) -> std::vector<TagScore>;

// Per-candidate document frequency across the analyzed corpus (how many
// images fire each character candidate above the zero-evidence floor).
auto buildCharacterDf(std::vector<ImageItem> const& items)
  -> std::map<std::string, std::size_t>;

// A candidate is credible on its own confidence, or on cross-image
// agreement at the weaker confidence level.
auto isCredibleCandidate(
  TagScore const& candidate,
  std::map<std::string, std::size_t> const& characterDf
) -> bool;

// True when a subject-count tag is asserted at or above
// kSubjectCountConfidence (a strong multi-subject assertion).
auto hasStrongCountTag(AnalysisResult const& analysis) -> bool;

// True when a subject-count tag (general category) is present.
bool isMultiSubject(AnalysisResult const& analysis);

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
std::string claimedTag(FolderReference const& reference);

// The folder claiming `tag` with the most tagged members, or nullptr.
auto owningFolder(std::vector<FolderReference> const& references, std::string const& tag)
  -> FolderReference const*;

}  // namespace organize
