#include "organize/execute.h"

#include "core/sha256.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
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
  return core::sha256Hex(bytes);
}

}  // namespace

namespace {

// How many copies of "<stem><ext>" already exist in the folder; the next
// free slot for a different-content collision is stem_<n><ext>.
auto resolveDestination(fs::path const& folderDir, ImageItem const& item, bool dryRun)
  -> std::optional<fs::path> {
  auto ec = std::error_code{};
  auto destination = folderDir / item.path.filename();
  if (dryRun || !fs::exists(destination, ec)) { return destination; }

  // Same-named file already there: identical content is a no-op, a
  // different one keeps both via a numeric suffix.
  auto const existingHash = fileHashOf(destination);
  if (!existingHash.empty() && existingHash == item.contentHash) { return std::nullopt; }
  auto const stem = item.path.stem().string();
  auto const extension = item.path.extension().string();
  auto suffix = 2;
  for (;; ++suffix) {
    destination = folderDir / std::format("{}_{}{}", stem, suffix, extension);
    if (!fs::exists(destination, ec)) { return destination; }
  }
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
    auto const folderExisted = dryRun ? false : fs::exists(folderDir, ec);
    if (!dryRun) { fs::create_directories(folderDir, ec); }

    auto const destination = resolveDestination(folderDir, item, dryRun);
    if (!destination.has_value()) {
      ++stats.skippedExisting;
      continue;
    }
    if (dryRun) { continue; }
    if (!folderExisted) { ++stats.createdFolders; }
    if (copySafely(item.path, stagingDir, *destination)) {
      ++stats.copied;
    } else {
      stats.errors.push_back(
        std::format("copy failed: {} -> {}", item.path.string(), destination->string())
      );
    }
  }
  return stats;
}

}  // namespace organize
