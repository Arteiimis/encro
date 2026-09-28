#include "organize/teach.h"

#include "core/display_text.h"
#include "core/sha256.h"
#include "organize/cluster.h"

#include <algorithm>
#include <cstddef>
#include <system_error>

namespace organize {

namespace {

// Folds one folder member into the reference. A member with no cached feature
// counts as analyzable (routing happened) but adds nothing to the mean.
void accumulateMember(FolderReference& reference, AnalysisResult const& analysis) {
  ++reference.analyzableMembers;

  auto const candidates = positiveCharacterTags(analysis);
  if (candidates.size() == 1) { ++reference.soleTagCounts[candidates.front().tag]; }

  if (analysis.identity.empty()) { return; }
  accumulateFeature(reference.meanFeature, reference.featureMembers, analysis.identity);
}

auto buildReference(fs::path const& folderDir, AnalysisCache const& cache)
  -> FolderReference {
  auto reference = FolderReference{.name = folderDir.filename()};
  auto ec = std::error_code{};
  for (auto const& member: fs::directory_iterator{folderDir, ec}) {
    if (!member.is_regular_file()) { continue; }
    auto const digest = core::sha256File(member.path());
    // An unreadable file hashes to "", which is also a reachable cache key:
    // the analysis stage stores under whatever scan computed. Skip instead of
    // looking it up, so an unrelated analysis never joins the reference.
    if (digest.empty()) { continue; }
    auto const cached = cache.get(digest);
    if (!cached.has_value()) { continue; }
    accumulateMember(reference, *cached);
  }
  return reference;
}

}  // namespace

auto buildFolderReferences(fs::path const& root, AnalysisCache const& cache)
  -> std::vector<FolderReference> {
  auto const outputRoot = root / "organized";
  auto ec = std::error_code{};
  if (!fs::exists(outputRoot, ec) || ec) { return {}; }

  auto entries = std::vector<fs::path>{};
  for (auto const& entry: fs::directory_iterator{outputRoot, ec}) {
    // Skip cache-internal directories; only character/unknown/mixed folders
    // teach.
    if (!entry.is_directory() || entry.path().filename() == ".cache") { continue; }
    entries.push_back(entry.path());
  }
  std::sort(entries.begin(), entries.end(), [](fs::path const& a, fs::path const& b) {
    // UTF-8 order, like the names this list is compared against.
    return displaytext::pathToUtf8String(a) < displaytext::pathToUtf8String(b);
  });

  auto references = std::vector<FolderReference>{};
  for (auto const& entry: entries) {
    auto reference = buildReference(entry, cache);
    if (reference.analyzableMembers > 0) { references.push_back(std::move(reference)); }
  }
  return references;
}

}  // namespace organize
