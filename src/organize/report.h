// End-of-run report: per-folder counts with assignment sources and run
// totals (spec "Progress and report").
#pragma once

#include "organize/organize_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace organize {

struct FolderReportLine {
  std::string folder;
  std::size_t images = 0;
  FolderSource source = FolderSource::Uncategorized;
};

struct ReportData {
  std::vector<FolderReportLine> folders;  // sorted by folder name
  std::size_t scanned = 0;
  std::size_t copied = 0;
  std::size_t skippedExisting = 0;
  std::size_t cacheHits = 0;
};

// Aggregates the per-folder view from the final assignment state.
auto buildFoldersSection(std::vector<ImageItem> const& items)
  -> std::vector<FolderReportLine>;

}  // namespace organize
