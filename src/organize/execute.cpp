#include "organize/execute.h"

#include "core/sha256.h"
#include "infra/stop_signal.h"

#include <algorithm>
#include <format>
#include <optional>
#include <system_error>

namespace organize {

namespace {

// Stages the source bytes outside the visible output tree
// (organized/.cache/tmp) so an interrupted copy never leaves a partial image
// behind. The caller creates the destination folder only once this
// succeeded (strict lazy creation), then finalizeCopy() completes it.
auto stageCopy(
  fs::path const& source,
  fs::path const& stagingDir,
  fs::path const& destination
) -> std::optional<fs::path> {
  auto ec = std::error_code{};
  auto const tempPath =
    stagingDir / std::format("{}.part", destination.filename().string());
  fs::copy_file(source, tempPath, fs::copy_options::overwrite_existing, ec);
  if (ec) { return std::nullopt; }
  return tempPath;
}

// Atomic final rename within the same volume; a failed rename drops the
// staging file so the cache directory never accumulates .part litter.
bool finalizeCopy(fs::path const& staged, fs::path const& destination) {
  auto ec = std::error_code{};
  fs::rename(staged, destination, ec);
  if (ec) {
    fs::remove(staged, ec);
    return false;
  }
  return true;
}

// How many copies of "<stem><ext>" already exist in the folder; the next
// free slot for a different-content collision is stem_<n><ext>.
auto resolveDestination(fs::path const& folderDir, ImageItem const& item, bool dryRun)
  -> std::optional<fs::path> {
  auto ec = std::error_code{};
  auto destination = folderDir / item.path.filename();
  if (dryRun || !fs::exists(destination, ec)) { return destination; }

  // Same-named file already there: identical content is a no-op, a
  // different one keeps both via a numeric suffix.
  auto const existingHash = core::sha256File(destination);
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
    // A stop request ends the copy phase at the next image: the copy in flight
    // was already renamed into place, and the next run skips what is there.
    if (stopsignal::isStopRequested()) {
      stats.canceled = true;
      break;
    }

    // folderName is already sanitized at routing time; empty -> uncategorized.
    auto const folder =
      item.folderName.empty() ? fs::path{kUncategorizedFolder} : item.folderName;

    auto const folderDir = outputRoot / folder;
    auto const destination = resolveDestination(folderDir, item, dryRun);
    if (!destination.has_value()) {
      ++stats.skippedExisting;
      continue;
    }
    if (dryRun) { continue; }

    // Stage the bytes first: a failed copy must not leave an empty folder
    // behind (strict lazy creation, design D7).
    auto const staged = stageCopy(item.path, stagingDir, *destination);
    if (!staged.has_value()) {
      stats.errors.push_back(
        std::format("copy failed: {} -> {}", item.path.string(), destination->string())
      );
      continue;
    }

    auto const folderExisted = fs::exists(folderDir, ec);
    fs::create_directories(folderDir, ec);
    if (!folderExisted) { ++stats.createdFolders; }
    if (finalizeCopy(*staged, *destination)) {
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
