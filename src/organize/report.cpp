#include "organize/report.h"

#include "core/display_text.h"

#include <format>
#include <map>
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

}  // namespace

auto renderReport(ReportData const& report) -> std::string {
  auto text = std::string{};
  text += "folder                          images  source\n";
  // The rule shares the encode plan's glyph family (pipeline-narration);
  // its 46 glyphs match the header row's width.
  text += displaytext::boxRule(46);
  text += '\n';
  for (auto const& folder: report.folders) {
    text += std::format(
      "{:<30} {:>6}  {}\n",
      folder.folder,
      folder.images,
      sourceLabel(folder.source)
    );
  }
  text += std::format(
    "\nscanned {} images: copied {}, skipped existing {} ({} cache hits)\n",
    report.scanned,
    report.copied,
    report.skippedExisting,
    report.cacheHits
  );
  for (auto const& error: report.copyErrors) { text += std::format("  {}\n", error); }
  return text;
}

}  // namespace organize
