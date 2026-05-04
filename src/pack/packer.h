#pragma once

#include "core/error_handle.h"
#include "core/progress.h"
#include "pack/pack.h"
#include "pack/pack_types.h"
#include "pack/packer_types.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_set>
#include <vector>

namespace pack {

class Packer final {
public:
  Packer() = default;

  auto packFilesToZip(
    std::vector<std::filesystem::path> const& filePaths,
    std::filesystem::path const& zipFilePath,
    progress::ProgressContext& progressCtx,
    std::string_view progressText,
    pack::detail::ZipEntryNameResolver entryNameForFile = {}
  ) -> eh::Result<void>;

  auto packFilesToZip(
    std::vector<PackFileEntry> const& entries,
    std::filesystem::path const& zipFilePath,
    progress::ProgressContext& progressCtx,
    std::string_view progressText
  ) -> eh::Result<void>;

  auto packFilesToZip(
    std::vector<PackFileEntry> const& entries,
    std::filesystem::path const& zipFilePath,
    pack::detail::PackEntryProgressCallback onEntryPacked = {},
    std::atomic<std::size_t>* finalizingCount = nullptr
  ) -> eh::Result<void>;

  auto groupFilesBySize(
    std::vector<std::filesystem::path> const& filePaths,
    std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
    std::optional<std::size_t> maxFilesPerGroup = std::nullopt
  ) -> std::vector<std::vector<std::filesystem::path>>;

  auto groupPackFiles(
    std::vector<pack::detail::PackGroupInput> const& filePaths,
    std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
    std::optional<std::size_t> maxFilesPerGroup = std::nullopt,
    std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
  ) -> std::vector<std::vector<std::filesystem::path>>;

  auto groupPackFilesWithSubparts(
    std::vector<pack::detail::PackGroupInput> const& filePaths,
    std::uintmax_t maxGroupSize,
    std::size_t maxFilesPerPart,
    std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
  ) -> std::vector<pack::detail::PackGroupPartition>;

  auto groupPackEntries(
    std::vector<pack::detail::PackEntryInput> const& entries,
    std::uintmax_t maxGroupSize = 490 * 1024 * 1024,
    std::optional<std::size_t> maxFilesPerGroup = std::nullopt,
    std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
  ) -> std::vector<std::vector<PackFileEntry>>;

  auto groupPackEntriesWithSubparts(
    std::vector<pack::detail::PackEntryInput> const& entries,
    std::uintmax_t maxGroupSize,
    std::size_t maxFilesPerPart,
    std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed = std::nullopt
  ) -> std::vector<pack::detail::PackEntryPartition>;

  auto buildDirectoryPackPlan(
    std::filesystem::path const& dirPath,
    std::filesystem::path const& zipFileDir,
    std::uintmax_t maxGroupSize = pack::kDefaultMaxArchiveGroupSize,
    bool recursive = true,
    NamingStrategy namingStrategy = NamingStrategy::Flat,
    std::optional<std::size_t> maxParallelJobs = std::nullopt,
    std::optional<std::filesystem::path> excludedPath = std::nullopt
  ) -> eh::Result<PackPlan>;

private:
  struct PreparedPackEntry;
  struct PreparedPackChunk;

  static auto normalizeZipEntryName(std::string const& entryName) -> std::string;
  static auto makeUniqueZipEntryName(
    std::string const& preferredEntryName,
    std::filesystem::path const& filePath,
    std::unordered_set<std::string>& usedEntryNames
  ) -> std::string;
  static auto buildConflictHandledPackEntryName(
    std::filesystem::path const& dirPath,
    std::filesystem::path const& filePath
  ) -> std::string;

  static auto wouldExceedGroupLimits(
    std::uintmax_t currentSize,
    std::size_t currentCount,
    std::uintmax_t additionalSize,
    std::size_t additionalCount,
    std::uintmax_t maxGroupSize,
    std::optional<std::size_t> maxFilesPerGroup
  ) -> bool;
  static void flushGroupedEntries(
    std::vector<PackFileEntry>& currentGroup,
    std::uintmax_t& currentSize,
    std::size_t& currentCount,
    std::vector<std::vector<PackFileEntry>>& groupedEntries
  );
  static void flushPreparedChunk(
    PreparedPackChunk& currentChunk,
    std::vector<PreparedPackChunk>& chunks
  );
  static auto groupPreparedEntriesSequentially(
    std::vector<PreparedPackEntry> const& preparedEntries,
    std::uintmax_t maxGroupSize,
    std::optional<std::size_t> maxFilesPerGroup
  ) -> std::vector<std::vector<PackFileEntry>>;
  static auto groupPreparedEntries(
    std::vector<PreparedPackEntry> preparedEntries,
    std::uintmax_t maxGroupSize,
    std::optional<std::size_t> maxFilesPerGroup,
    std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed
  ) -> std::vector<std::vector<PackFileEntry>>;
  static auto splitSourceDirectoryEntries(
    std::vector<PreparedPackEntry> const& entries,
    std::uintmax_t maxGroupSize,
    std::optional<std::size_t> maxFilesPerGroup
  ) -> std::vector<PreparedPackChunk>;
  static void packSourceEntryChunks(
    std::vector<PreparedPackEntry> const& entries,
    std::uintmax_t maxGroupSize,
    std::optional<std::size_t> maxFilesPerGroup,
    std::vector<PackFileEntry>& currentGroup,
    std::uintmax_t& currentSize,
    std::size_t& currentCount,
    std::vector<std::vector<PackFileEntry>>& groupedEntries
  );
  static auto buildPackEntryStableKey(PackFileEntry const& entry) -> std::string;
  static auto sourcePathsForGroup(std::vector<PackFileEntry> const& entries)
    -> std::vector<std::filesystem::path>;
  static auto
  sourcePathGroups(std::vector<std::vector<PackFileEntry>> const& groupedEntries)
    -> std::vector<std::vector<std::filesystem::path>>;
  static void runFinalizingSpinner(
    progress::ProgressContext& progressCtx,
    std::size_t progressBarIndex,
    std::string_view progressText,
    std::atomic<bool>& finalizing,
    std::stop_token stopToken
  );
};

}  // namespace pack
