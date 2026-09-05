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

// Builds references from the folders currently under <root>/organized.
// Uses the cache for member analysis; members missing from the cache are
// ignored (rebuildable fast path in design D6). Folders with no analyzable
// members are skipped.
auto buildFolderReferences(
  fs::path const& root,
  AnalysisCache const& cache,
  double minConfidence
) -> std::vector<FolderReference>;

}  // namespace organize
