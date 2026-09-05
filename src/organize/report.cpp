#include "organize/report.h"

#include <map>

namespace organize {

auto buildFoldersSection(std::vector<ImageItem> const& items)
  -> std::vector<FolderReportLine> {
  auto const order = [](FolderSource source) { return static_cast<int>(source); };
  // folder name -> (count, highest-priority source seen)
  auto counts = std::map<std::string, std::pair<std::size_t, FolderSource>>{};
  for (auto const& item: items) {
    auto& [count, source] = counts[item.folderName];
    count += 1;
    if (order(item.folderSource) < order(source)) { source = item.folderSource; }
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

}  // namespace organize
