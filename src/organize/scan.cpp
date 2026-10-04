#include "organize/scan.h"

#include "core/media_scanner.h"
#include "core/sha256.h"

#include <algorithm>

namespace organize {

namespace {

// The output tree lives at <root>/organized and must never re-enter the scan
// (recursive runs would otherwise ingest previous results).
bool isInsideOutputTree(fs::path const& candidate, fs::path const& root) {
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

// Hashes each match and drops anything inside the output tree.
auto hashedItems(fs::path const& root, std::vector<fs::path> const& paths)
  -> std::vector<ImageItem> {
  auto items = std::vector<ImageItem>{};
  items.reserve(paths.size());
  for (auto const& path: paths) {
    if (isInsideOutputTree(path, root)) { continue; }
    items.push_back(ImageItem{.path = path, .contentHash = core::sha256File(path)});
  }
  return items;
}

}  // namespace

auto scanImages(
  fs::path const& root,
  bool recursive,
  std::vector<FolderDisposition> const& dispositions
) -> eh::Result<std::vector<ImageItem>> {
  if (dispositions.empty()) {
    auto scanRes = media::scanByExtensions(root, kImageExtensions, recursive);
    if (!scanRes) { return std::unexpected(std::move(scanRes).error()); }
    return hashedItems(root, scanRes->matches);
  }

  // Disposition-driven input assembly: the root's loose images plus each
  // input folder collected recursively (recursive runs dispose every
  // non-ignored folder as input, so the whole tree is scanned as before).
  auto items = std::vector<ImageItem>{};
  auto loose = media::scanByExtensions(root, kImageExtensions, false);
  if (!loose) { return std::unexpected(std::move(loose).error()); }
  items = hashedItems(root, loose->matches);
  for (auto const& disposition: dispositions) {
    if (disposition.kind != DispositionKind::Input) { continue; }
    auto scanned =
      media::scanByExtensions(root / disposition.name, kImageExtensions, true);
    if (!scanned) { return std::unexpected(std::move(scanned).error()); }
    auto hashed = hashedItems(root, scanned->matches);
    items.insert(
      items.end(),
      std::make_move_iterator(hashed.begin()),
      std::make_move_iterator(hashed.end())
    );
  }
  return items;
}

}  // namespace organize
