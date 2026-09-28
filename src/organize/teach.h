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

// Builds references from the folders currently under <root>/organized, in
// folder-name order so that a cluster matching several references at the same
// score always lands in the same one. Uses the cache for member analysis;
// members missing from the cache are ignored (rebuildable fast path in design
// D6). Folders with no analyzable members are skipped.
auto buildFolderReferences(fs::path const& root, AnalysisCache const& cache)
  -> std::vector<FolderReference>;

// Merges this run's character-folder routing into the on-disk references, keyed
// by display name (design D1-D3). mixed/ and uncategorized/ never become
// references, and the result is in name order like the on-disk list.
auto buildSameRunReferences(
  std::vector<ImageItem> const& items,
  std::vector<FolderReference> const& onDisk
) -> std::vector<FolderReference>;

}  // namespace organize
