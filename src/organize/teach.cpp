#include "organize/teach.h"

#include "core/display_text.h"
#include "core/sha256.h"
#include "organize/cluster.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <system_error>
#include <utility>

namespace organize {

namespace {

// Folds one folder member into the reference. A member with no cached feature
// counts as analyzable (routing happened) but adds nothing to the mean.
void accumulateMember(FolderReference& reference, AnalysisResult const& analysis) {
  ++reference.analyzableMembers;

  auto const candidates = positiveCharacterTags(analysis);
  if (candidates.size() == 1) { ++reference.soleTagCounts[candidates.front().tag]; }

  if (!analysis.identity.empty()) {
    accumulateFeature(reference.meanFeature, reference.featureMembers, analysis.identity);
  }
  auto const tags = identityTagVector(analysis.tags.general);
  if (!tags.empty()) {
    accumulateTagVector(reference.meanTags, reference.tagMembers, tags);
  }
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

auto outputRootFolderNames(fs::path const& root) -> std::vector<fs::path> {
  auto const outputRoot = root / "organized";
  auto ec = std::error_code{};
  auto names = std::vector<fs::path>{};
  if (!fs::exists(outputRoot, ec) || ec) { return names; }
  for (auto const& entry: fs::directory_iterator{outputRoot, ec}) {
    // Skip cache-internal directories; only character/unknown/mixed folders
    // count.
    if (ec || !entry.is_directory() || entry.path().filename() == ".cache") { continue; }
    names.push_back(entry.path());
  }
  std::sort(names.begin(), names.end(), [](fs::path const& a, fs::path const& b) {
    // UTF-8 order, like the names this list is compared against.
    return displaytext::pathToUtf8String(a) < displaytext::pathToUtf8String(b);
  });
  return names;
}

auto buildFolderReferences(
  fs::path const& root,
  AnalysisCache const& cache,
  std::vector<FolderSample> const& firstLevelSamples
) -> std::vector<FolderReference> {
  auto const entries = outputRootFolderNames(root);

  // Keyed by display name so a first-level sample of the same name folds
  // into the on-disk reference: one reference, union membership (design D5).
  auto byName = std::map<std::string, FolderReference>{};
  for (auto const& entry: entries) {
    auto reference = buildReference(entry, cache);
    byName.emplace(displaytext::pathToUtf8String(reference.name), std::move(reference));
  }
  for (auto const& sample: firstLevelSamples) {
    auto& reference = byName[displaytext::pathToUtf8String(sample.name)];
    if (reference.name.empty()) { reference.name = sample.name; }
    for (auto const& member: sample.members) {
      if (!member.analysis.has_value()) { continue; }
      accumulateMember(reference, *member.analysis);
    }
  }

  auto references = std::vector<FolderReference>{};
  references.reserve(byName.size());
  for (auto& entry: byName) {
    if (entry.second.analyzableMembers > 0) {
      references.push_back(std::move(entry.second));
    }
  }
  // Map iteration is name order, the invariant a tie in the capture relies on.
  return references;
}

auto buildSameRunReferences(
  std::vector<ImageItem> const& items,
  std::vector<FolderReference> const& onDisk
) -> std::vector<FolderReference> {
  // Keyed by display name: a destination and an on-disk folder of the same
  // name become one reference.
  auto byName = std::map<std::string, FolderReference>{};
  for (auto const& reference: onDisk) {
    byName[displaytext::pathToUtf8String(reference.name)] = reference;
  }

  // This run's filed images, folded in content-hash order: the stored mean must
  // not depend on the order the scanner happened to walk the gallery. The
  // analysis is held by pointer so the fold below needs no second check.
  struct Filed {
    std::string hash;
    fs::path destination;
    AnalysisResult const* analysis = nullptr;
  };
  auto filed = std::vector<Filed>{};
  for (auto const& item: items) {
    // Only a character folder can teach one: mixed/ holds several characters
    // and uncategorized/ may hold none (design D1).
    if (
      item.folderSource != FolderSource::CharacterTag
      && item.folderSource != FolderSource::FolderMatch
    ) {
      continue;
    }
    if (item.folderName.empty() || !item.analysis.has_value()) { continue; }
    filed.push_back(
      Filed{
        .hash = item.contentHash,
        .destination = item.folderName,
        .analysis = &*item.analysis,
      }
    );
  }
  std::sort(filed.begin(), filed.end(), [](Filed const& a, Filed const& b) {
    return a.hash < b.hash;
  });

  for (auto const& entry: filed) {
    auto& reference = byName[displaytext::pathToUtf8String(entry.destination)];
    // A destination the routing created has no name yet; an on-disk reference
    // keeps the one the user sees.
    if (reference.name.empty()) { reference.name = entry.destination; }
    accumulateMember(reference, *entry.analysis);
  }

  auto references = std::vector<FolderReference>{};
  references.reserve(byName.size());
  for (auto& entry: byName) { references.push_back(std::move(entry.second)); }
  // Name order is the invariant a tie in the capture relies on, so it is stated
  // here in the same terms buildFolderReferences uses rather than left to the
  // map's comparator.
  std::sort(
    references.begin(),
    references.end(),
    [](FolderReference const& a, FolderReference const& b) {
      return displaytext::pathToUtf8String(a.name)
        < displaytext::pathToUtf8String(b.name);
    }
  );
  return references;
}

}  // namespace organize
