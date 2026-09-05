// Execution stage: copy classified images into per-character folders without
// ever touching the originals (spec "Output semantics"). Skip-existing
// compares content hashes, not sizes.
#pragma once

#include "organize/organize_types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace organize {

inline constexpr auto kUncategorizedFolder = "uncategorized";
inline constexpr auto kMixedFolder = "mixed";

struct ExecuteStats {
  std::size_t copied = 0;
  std::size_t skippedExisting = 0;
  std::size_t createdFolders = 0;   // folders that did not exist before this run
  std::vector<std::string> errors;  // per-file copy failures; never fatal
};

// Copies every item into <root>/organized/<folderName>/ (items without an
// assignment go to uncategorized/). One destination per item, no partial
// copies on failure (temp file + rename). An existing destination with
// identical content hash is skipped; different content gets a suffix.
// `dryRun` performs everything except the actual copies/folder creation.
auto executeOrganize(
  fs::path const& root,
  std::vector<ImageItem> const& items,
  bool dryRun = false
) -> ExecuteStats;

}  // namespace organize
