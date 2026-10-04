#include "organize/report.h"

#include "core/display_text.h"

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace organize {

auto buildFoldersSection(std::vector<ImageItem> const& items)
  -> std::vector<FolderReportLine> {
  auto const order = [](FolderSource source) { return static_cast<int>(source); };
  // folder name -> (count, highest-priority source seen)
  auto counts = std::map<std::string, std::pair<std::size_t, FolderSource>>{};
  for (auto const& item: items) {
    auto const key = displaytext::pathToUtf8String(item.folderName);
    auto [entry, inserted] = counts.try_emplace(key, 1, item.folderSource);
    if (inserted) { continue; }
    entry->second.first += 1;
    // Highest-priority source seen: the lowest enum ordinal wins.
    if (order(item.folderSource) < order(entry->second.second)) {
      entry->second.second = item.folderSource;
    }
  }

  auto folders = std::vector<FolderReportLine>{};
  folders.reserve(counts.size());
  for (auto const& [folder, countAndSource]: counts) {
    folders.push_back(
      FolderReportLine{
        .folder = folder,
        .images = countAndSource.first,
        .source = countAndSource.second,
      }
    );
  }
  return folders;
}

namespace {

auto sourceLabel(FolderSource source) -> std::string_view {
  switch (source) {
    case FolderSource::CharacterTag : return "character tag";
    case FolderSource::FolderMatch  : return "folder match";
    case FolderSource::NewCluster   : return "new cluster";
    case FolderSource::Mixed        : return "mixed";
    case FolderSource::Uncategorized: return "uncategorized";
  }
  return "unknown";
}

auto kindLabel(DispositionLine const& line) -> std::string_view {
  if (line.demoted) { return "demoted"; }
  switch (line.kind) {
    case DispositionKind::Reference: return "reference";
    case DispositionKind::Input    : return "input";
    case DispositionKind::Ignored  : return "ignored";
  }
  return "unknown";
}

// One rendered table row: a counted folder, or a zero-count row for a
// reference folder nothing matched (design D9).
struct ReportRow {
  std::string folder;
  std::size_t images = 0;
  std::string_view label;
};

// The folder column's width (encode-probe table layout): at least the fixed
// minimum (today's layout, kept for short-name tables), never wider than the
// terminal budget after the fixed tail (space + images 6 + gap 2 + the
// longest source label 13). A narrower terminal falls back to the minimum,
// matching the pre-dynamic fixed layout.
constexpr auto kMinFolderWidth = std::size_t{30};
constexpr auto kTailWidth = std::size_t{9 + 13};

auto resolveFolderWidth(std::vector<ReportRow> const& rows, std::size_t terminalColumns)
  -> std::size_t {
  auto widest = std::size_t{0};
  for (auto const& row: rows) {
    widest = std::max(widest, displaytext::displayWidth(row.folder));
  }
  auto const budget = terminalColumns > kTailWidth ? terminalColumns - kTailWidth : 0;
  return std::min(std::max(widest, kMinFolderWidth), std::max(budget, kMinFolderWidth));
}

}  // namespace

auto renderReport(ReportData const& report, std::size_t terminalColumns) -> std::string {
  // Counted folders plus zero-count rows for non-demoted reference folders
  // the run filed nothing into, merged in folder-name order (design D9).
  auto rows = std::vector<ReportRow>{};
  rows.reserve(report.folders.size() + report.dispositions.size());
  for (auto const& folder: report.folders) {
    rows.push_back(
      ReportRow{
        .folder = folder.folder,
        .images = folder.images,
        .label = sourceLabel(folder.source)
      }
    );
  }
  auto counted = std::set<std::string>{};
  for (auto const& folder: report.folders) { counted.insert(folder.folder); }
  for (auto const& disposition: report.dispositions) {
    if (disposition.demoted || disposition.kind != DispositionKind::Reference) {
      continue;
    }
    if (counted.contains(disposition.folder)) { continue; }
    rows.push_back(
      ReportRow{.folder = disposition.folder, .images = 0, .label = "reference"}
    );
  }
  std::sort(rows.begin(), rows.end(), [](ReportRow const& a, ReportRow const& b) {
    return a.folder < b.folder;
  });

  auto text = std::string{};
  auto const nameWidth = resolveFolderWidth(rows, terminalColumns);
  auto const headerLine = std::format(
    "{} {:>6}  {}",
    displaytext::padToDisplayWidth("folder", nameWidth),
    "images",
    "source"
  );
  text += headerLine;
  text += '\n';
  // The rule shares the encode plan's glyph family (pipeline-narration); its
  // width matches the rendered header row.
  text += displaytext::boxRule(displaytext::displayWidth(headerLine));
  text += '\n';
  for (auto const& row: rows) {
    text += std::format(
      "{} {:>6}  {}\n",
      displaytext::padToDisplayWidth(
        displaytext::truncateWithEllipsis(row.folder, nameWidth),
        nameWidth
      ),
      row.images,
      row.label
    );
  }
  text += std::format(
    "\nscanned {} images: copied {}, skipped existing {} ({} cache hits)\n",
    report.scanned,
    report.copied,
    report.skippedExisting,
    report.cacheHits
  );
  // Disposition summary, incremental runs only: every first-level folder,
  // demoted rows carrying the --ingest hint.
  if (!report.dispositions.empty()) {
    text += "\ndispositions:\n";
    for (auto const& disposition: report.dispositions) {
      text += std::format("  {}: {}", disposition.folder, kindLabel(disposition));
      if (disposition.demoted) {
        text +=
          std::format(" (rerun with --ingest {} to sort it in)", disposition.folder);
      }
      text += '\n';
    }
  }
  for (auto const& error: report.copyErrors) { text += std::format("  {}\n", error); }
  return text;
}

}  // namespace organize
