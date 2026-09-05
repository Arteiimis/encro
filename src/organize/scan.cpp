#include "organize/scan.h"

#include "core/media_scanner.h"
#include "core/sha256.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace organize {

namespace {

auto fileHash(fs::path const& path) -> std::string {
  auto file = std::ifstream{path, std::ios::binary};
  if (!file.is_open()) { return {}; }
  auto const bytes = std::string{std::istreambuf_iterator<char>{file}, {}};
  return core::sha256Hex(bytes);
}

// The output tree lives at <root>/organized and must never re-enter the scan
// (recursive runs would otherwise ingest previous results).
auto isInsideOutputTree(fs::path const& candidate, fs::path const& root) -> bool {
  auto const outputRoot = std::filesystem::weakly_canonical(root / "organized");
  auto const normalized = std::filesystem::weakly_canonical(candidate);
  auto const [mismatch, _] = std::mismatch(
    outputRoot.begin(),
    outputRoot.end(),
    normalized.begin(),
    normalized.end()
  );
  return mismatch == outputRoot.end();
}

}  // namespace

auto scanImages(fs::path const& root, bool recursive)
  -> eh::Result<std::vector<ImageItem>> {
  auto scanRes = media::scanByExtensions(root, kImageExtensions, recursive);
  if (!scanRes) { return std::unexpected(std::move(scanRes).error()); }

  auto items = std::vector<ImageItem>{};
  items.reserve(scanRes->matches.size());
  for (auto const& path: scanRes->matches) {
    if (isInsideOutputTree(path, root)) { continue; }
    items.push_back(ImageItem{.path = path, .contentHash = fileHash(path)});
  }
  return items;
}

}  // namespace organize
