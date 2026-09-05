#include "organize/execute.h"

#include "organize/sha256.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace organize {

namespace {

// Copies via a staging file outside the visible output tree (organized/
// .cache/tmp), so an interrupted copy never leaves a partial image behind;
// the final rename is atomic within the same volume.
auto copySafely(
  fs::path const& source,
  fs::path const& stagingDir,
  fs::path const& destination
) -> bool {
  auto ec = std::error_code{};
  auto const tempPath =
    stagingDir / std::format("{}.part", destination.filename().string());
  fs::copy_file(source, tempPath, fs::copy_options::overwrite_existing, ec);
  if (ec) { return false; }
  fs::rename(tempPath, destination, ec);
  if (ec) {
    fs::remove(tempPath, ec);
    return false;
  }
  return true;
}

auto fileHashOf(fs::path const& path) -> std::string {
  auto file = std::ifstream{path, std::ios::binary};
  if (!file.is_open()) { return {}; }
  auto const bytes = std::string{std::istreambuf_iterator<char>{file}, {}};
  return sha256Hex(bytes);
}

}  // namespace

auto executeOrganize(
  fs::path const& root,
  std::vector<ImageItem> const& items,
  bool dryRun
) -> ExecuteStats {
  auto stats = ExecuteStats{};
  auto ec = std::error_code{};
  auto const outputRoot = root / "organized";
  auto const stagingDir = outputRoot / ".cache" / "tmp";
  if (!dryRun) {
    fs::create_directories(outputRoot, ec);
    fs::create_directories(stagingDir, ec);
  }

  for (auto const& item: items) {
    // folderName is already sanitized at routing time; empty -> uncategorized.
    auto const folder =
      item.folderName.empty() ? fs::path{kUncategorizedFolder} : item.folderName;

    auto const folderDir = outputRoot / folder;
    auto folderExisted = dryRun ? false : fs::exists(folderDir, ec);
    if (!dryRun) { fs::create_directories(folderDir, ec); }

    auto destination = folderDir / item.path.filename();
    if (!dryRun && fs::exists(destination, ec)) {
      // Same-named file already there: identical content is a no-op, a
      // different one keeps both via a numeric suffix.
      auto const sameContent =
        !fileHashOf(destination).empty() && fileHashOf(destination) == item.contentHash;
      if (sameContent) {
        ++stats.skippedExisting;
        continue;
      }
      auto const stem = item.path.stem().string();
      auto const extension = item.path.extension().string();
      auto suffix = 2;
      for (;; ++suffix) {
        destination = folderDir / std::format("{}_{}{}", stem, suffix, extension);
        if (!fs::exists(destination, ec)) { break; }
      }
    }

    if (dryRun) { continue; }
    if (!folderExisted) { ++stats.createdFolders; }
    if (copySafely(item.path, stagingDir, destination)) {
      ++stats.copied;
    } else {
      stats.errors.push_back(
        std::format("copy failed: {} -> {}", item.path.string(), destination.string())
      );
    }
  }
  return stats;
}

}  // namespace organize
