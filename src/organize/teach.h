// Teaching references from the existing output tree (task 3.4, design D6):
// every current folder under organized/ becomes a reference computed from
// its cached analyzable members.
#pragma once

#include "organize/assign.h"
#include "organize/cache.h"

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace organize {

// One first-level reference folder's sampled members (design D3): collected
// recursively, ordered and capped by content hash, analyses filled from the
// shared cache by the sampling stage. Members exist to build the folder's
// profile — they never route or copy.
struct FolderSample {
  fs::path name;
  std::vector<ImageItem> members;
};

// Builds references from the folders currently under <root>/organized plus —
// when given — the first-level reference folders' samples, merging same-name
// sources into one reference with the union membership. In folder-name order
// so that a cluster matching several references at the same score always
// lands in the same one. Uses the cache for organized/ member analysis;
// members missing from the cache are ignored (rebuildable fast path in design
// D6). Folders with no analyzable members are skipped. The caller drops
// demoted folders from `firstLevelSamples` before calling.
auto buildFolderReferences(
  fs::path const& root,
  AnalysisCache const& cache,
  std::vector<FolderSample> const& firstLevelSamples = {}
) -> std::vector<FolderReference>;

// The character folders currently under <root>/organized (never .cache), in
// folder-name order — the name set both reference construction and the
// cluster-naming collision seed read.
auto outputRootFolderNames(fs::path const& root) -> std::vector<fs::path>;

// Merges this run's character-folder routing into the on-disk references, keyed
// by display name (design D1-D3). mixed/ and uncategorized/ never become
// references, and the result is in name order like the on-disk list.
auto buildSameRunReferences(
  std::vector<ImageItem> const& items,
  std::vector<FolderReference> const& onDisk
) -> std::vector<FolderReference>;

}  // namespace organize
