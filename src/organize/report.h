// End-of-run report: per-folder counts with assignment sources and run
// totals (spec "Progress and report").
#pragma once

#include "organize/disposition.h"
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

// One first-level folder's disposition as the report shows it (design D9):
// `demoted` marks a reference the mixed-sample check dropped, which the
// summary names with a hint at --ingest.
struct DispositionLine {
  std::string folder;
  DispositionKind kind = DispositionKind::Reference;
  bool demoted = false;
};

struct ReportData {
  std::vector<FolderReportLine> folders;  // sorted by folder name
  std::size_t scanned = 0;
  std::size_t copied = 0;
  std::size_t skippedExisting = 0;
  std::size_t cacheHits = 0;
  std::vector<std::string> copyErrors;  // per-file copy failures; visible in report
  // First-level dispositions; empty outside incremental runs, so the
  // disposition summary is an incremental-only report section.
  std::vector<DispositionLine> dispositions;
  // A stop request aborted the analysis or the copy phase: the data above is
  // partial and the report must not print.
  bool canceled = false;
};

// Aggregates the per-folder view from the final assignment state.
auto buildFoldersSection(std::vector<ImageItem> const& items)
  -> std::vector<FolderReportLine>;

// Human-readable report text (per-folder counts + sources, run totals). The
// folder column adapts to `terminalColumns` (encode-probe table layout):
// never below the fixed minimum, never wider than the terminal budget.
auto renderReport(ReportData const& report, std::size_t terminalColumns) -> std::string;

}  // namespace organize
