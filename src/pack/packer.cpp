#include "pack/packer.h"

#include "core/collision_naming.h"
#include "core/progress.h"
#include "infra/terminal.h"
#include "pack/pack_service.h"

#include <libzippp/libzippp.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <stop_token>
#include <thread>

namespace fs = std::filesystem;
using namespace indicators;
namespace naming = collisionnaming;

using enum terminal::MessageKind;

namespace {

auto buildConflictHandledPackEntryName(fs::path const& dirPath, fs::path const& filePath)
  -> std::string {
  return naming::buildConflictHandledFlatName(
    dirPath,
    filePath,
    filePath.stem().string(),
    filePath.extension().string()
  );
}

auto normalizeZipEntryName(std::string const& entryName) -> std::string {
  auto normalized = fs::path{entryName}.generic_string();
  while (!normalized.empty() && normalized.front() == '/') {
    normalized.erase(normalized.begin());
  }
  return normalized;
}

auto makeUniqueZipEntryName(
  std::string const& preferredEntryName,
  fs::path const& filePath,
  std::unordered_set<std::string>& usedEntryNames
) -> std::string {
  auto const normalizedEntryName = normalizeZipEntryName(preferredEntryName);
  if (usedEntryNames.insert(normalizedEntryName).second) { return normalizedEntryName; }

  auto const entryPath = fs::path{normalizedEntryName};
  auto const suffix = std::format("__{}", naming::shortPathHash(filePath));
  auto const parentPath = entryPath.parent_path();
  auto const stem = entryPath.stem().string();
  auto const extension = entryPath.extension().string();

  auto candidate =
    (parentPath / std::format("{}{}{}", stem, suffix, extension)).generic_string();
  auto duplicateIndex = std::size_t{1};
  while (!usedEntryNames.insert(candidate).second) {
    candidate =
      (parentPath / std::format("{}{}_{}{}", stem, suffix, duplicateIndex++, extension))
        .generic_string();
  }

  return candidate;
}

struct PreparedPackEntry {
  pack::PackFileEntry entry;
  std::string sourceKey;
  std::string fileKey;
  std::uintmax_t fileSize = 0;
};

struct PreparedPackChunk {
  std::vector<pack::PackFileEntry> entries;
  std::uintmax_t totalSize = 0;
  std::size_t fileCount = 0;
};

auto wouldExceedGroupLimits(
  std::uintmax_t currentSize,
  std::size_t currentCount,
  std::uintmax_t additionalSize,
  std::size_t additionalCount,
  std::uintmax_t maxGroupSize,
  std::optional<std::size_t> maxFilesPerGroup
) -> bool;

void flushGroupedEntries(
  std::vector<pack::PackFileEntry>& currentGroup,
  std::uintmax_t& currentSize,
  std::size_t& currentCount,
  std::vector<std::vector<pack::PackFileEntry>>& groupedEntries
);

auto groupPreparedEntriesSequentially(
  std::vector<PreparedPackEntry> const& preparedEntries,
  std::uintmax_t maxGroupSize,
  std::optional<std::size_t> maxFilesPerGroup
) -> std::vector<std::vector<pack::PackFileEntry>> {
  auto groupedEntries = std::vector<std::vector<pack::PackFileEntry>>{};
  auto currentGroup = std::vector<pack::PackFileEntry>{};
  auto currentSize = std::uintmax_t{0};
  auto currentCount = std::size_t{0};

  for (auto const& entry: preparedEntries) {
    if (
      !currentGroup.empty()
      && wouldExceedGroupLimits(
        currentSize,
        currentCount,
        entry.fileSize,
        1,
        maxGroupSize,
        maxFilesPerGroup
      )
    ) {
      flushGroupedEntries(currentGroup, currentSize, currentCount, groupedEntries);
    }

    currentGroup.emplace_back(entry.entry);
    currentSize += entry.fileSize;
    ++currentCount;
  }

  flushGroupedEntries(currentGroup, currentSize, currentCount, groupedEntries);
  return groupedEntries;
}

auto wouldExceedGroupLimits(
  std::uintmax_t currentSize,
  std::size_t currentCount,
  std::uintmax_t additionalSize,
  std::size_t additionalCount,
  std::uintmax_t maxGroupSize,
  std::optional<std::size_t> maxFilesPerGroup
) -> bool {
  if (currentSize + additionalSize > maxGroupSize) { return true; }
  return maxFilesPerGroup.has_value()
    && currentCount + additionalCount > maxFilesPerGroup.value();
}

void flushPreparedChunk(
  PreparedPackChunk& currentChunk,
  std::vector<PreparedPackChunk>& chunks
) {
  if (currentChunk.entries.empty()) { return; }
  chunks.emplace_back(std::move(currentChunk));
  currentChunk = {};
}

void flushGroupedEntries(
  std::vector<pack::PackFileEntry>& currentGroup,
  std::uintmax_t& currentSize,
  std::size_t& currentCount,
  std::vector<std::vector<pack::PackFileEntry>>& groupedEntries
) {
  if (currentGroup.empty()) { return; }
  groupedEntries.emplace_back(std::move(currentGroup));
  currentGroup = {};
  currentSize = 0;
  currentCount = 0;
}

auto splitSourceDirectoryEntries(
  std::vector<PreparedPackEntry> const& entries,
  std::uintmax_t maxGroupSize,
  std::optional<std::size_t> maxFilesPerGroup
) -> std::vector<PreparedPackChunk> {
  auto chunks = std::vector<PreparedPackChunk>{};
  auto currentChunk = PreparedPackChunk{};

  for (auto const& entry: entries) {
    if (
      !currentChunk.entries.empty()
      && wouldExceedGroupLimits(
        currentChunk.totalSize,
        currentChunk.fileCount,
        entry.fileSize,
        1,
        maxGroupSize,
        maxFilesPerGroup
      )
    ) {
      flushPreparedChunk(currentChunk, chunks);
    }

    currentChunk.entries.emplace_back(entry.entry);
    currentChunk.totalSize += entry.fileSize;
    ++currentChunk.fileCount;
  }

  flushPreparedChunk(currentChunk, chunks);
  return chunks;
}

auto buildPackEntryStableKey(pack::PackFileEntry const& entry) -> std::string {
  return std::format(
    "{}|{}",
    naming::stablePathString(entry.sourcePath),
    naming::stablePathString(fs::path{entry.zipEntryName})
  );
}

auto groupPreparedEntries(
  std::vector<PreparedPackEntry> preparedEntries,
  std::uintmax_t maxGroupSize,
  std::optional<std::size_t> maxFilesPerGroup,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed
) -> std::vector<std::vector<pack::PackFileEntry>> {
  if (preparedEntries.empty()) { return {}; }

  if (
    !keepSourceDirsTogetherWhenTotalFilesExceed.has_value()
    || preparedEntries.size() <= keepSourceDirsTogetherWhenTotalFilesExceed.value()
  ) {
    return groupPreparedEntriesSequentially(
      preparedEntries,
      maxGroupSize,
      maxFilesPerGroup
    );
  }

  std::ranges::sort(
    preparedEntries,
    [](PreparedPackEntry const& lhs, PreparedPackEntry const& rhs) {
      if (lhs.sourceKey != rhs.sourceKey) { return lhs.sourceKey < rhs.sourceKey; }
      return lhs.fileKey < rhs.fileKey;
    }
  );

  auto groupedEntries = std::vector<std::vector<pack::PackFileEntry>>{};
  auto currentGroup = std::vector<pack::PackFileEntry>{};
  auto currentSize = std::uintmax_t{0};
  auto currentCount = std::size_t{0};

  auto currentSourceEntries = std::vector<PreparedPackEntry>{};
  auto currentSourceKey = std::string{};

  auto packSourceEntries = [&](std::vector<PreparedPackEntry> const& entries) {
    auto const sourceChunks =
      splitSourceDirectoryEntries(entries, maxGroupSize, maxFilesPerGroup);
    for (auto const& chunk: sourceChunks) {
      if (
        !currentGroup.empty()
        && wouldExceedGroupLimits(
          currentSize,
          currentCount,
          chunk.totalSize,
          chunk.fileCount,
          maxGroupSize,
          maxFilesPerGroup
        )
      ) {
        flushGroupedEntries(currentGroup, currentSize, currentCount, groupedEntries);
      }

      currentGroup.insert(currentGroup.end(), chunk.entries.begin(), chunk.entries.end());
      currentSize += chunk.totalSize;
      currentCount += chunk.fileCount;
    }
  };

  for (auto const& entry: preparedEntries) {
    if (currentSourceEntries.empty()) {
      currentSourceKey = entry.sourceKey;
    } else if (entry.sourceKey != currentSourceKey) {
      packSourceEntries(currentSourceEntries);
      currentSourceEntries.clear();
      currentSourceKey = entry.sourceKey;
    }

    currentSourceEntries.emplace_back(entry);
  }

  packSourceEntries(currentSourceEntries);
  flushGroupedEntries(currentGroup, currentSize, currentCount, groupedEntries);

  return groupedEntries;
}

auto sourcePathsForGroup(std::vector<pack::PackFileEntry> const& entries)
  -> std::vector<fs::path> {
  auto paths = std::vector<fs::path>{};
  paths.reserve(entries.size());
  for (auto const& entry: entries) { paths.push_back(entry.sourcePath); }
  return paths;
}

auto sourcePathGroups(std::vector<std::vector<pack::PackFileEntry>> const& groupedEntries)
  -> std::vector<std::vector<fs::path>> {
  auto groupedPaths = std::vector<std::vector<fs::path>>{};
  groupedPaths.reserve(groupedEntries.size());
  for (auto const& group: groupedEntries) {
    groupedPaths.push_back(sourcePathsForGroup(group));
  }
  return groupedPaths;
}

}  // namespace

auto packFilesToZip(
  std::vector<fs::path> const& filePaths,
  fs::path const& zipFilePath,
  progress::ProgressContext& progressCtx,
  std::string_view progressText,
  ZipEntryNameResolver entryNameForFile
) -> eh::Result<void> {
  auto entries = std::vector<pack::PackFileEntry>{};
  entries.reserve(filePaths.size());
  for (auto const& filePath: filePaths) {
    entries.emplace_back(
      pack::PackFileEntry{
        .sourcePath = filePath,
        .zipEntryName =
          entryNameForFile ? entryNameForFile(filePath) : filePath.filename().string(),
      }
    );
  }

  return packFilesToZip(entries, zipFilePath, progressCtx, progressText);
}

auto packFilesToZip(
  std::vector<pack::PackFileEntry> const& entries,
  fs::path const& zipFilePath,
  progress::ProgressContext& progressCtx,
  std::string_view progressText
) -> eh::Result<void> try {
  auto zip = libzippp::ZipArchive(zipFilePath.string());
  auto fileCount = entries.size();
  auto const progressBarIndex = progressCtx.addBar(progressText, progress::Tone::Packing);
  auto usedEntryNames = std::unordered_set<std::string>{};

  zip.open(libzippp::ZipArchive::New);

  for (auto const& [index, entry]: std::views::enumerate(entries)) {
    auto const progress = (size_t)std::round((index + 1) / (float)fileCount * 100.0f);

    if (fs::is_regular_file(entry.sourcePath)) {
      auto const entryName =
        makeUniqueZipEntryName(entry.zipEntryName, entry.sourcePath, usedEntryNames);
      zip.addFile(entryName, entry.sourcePath.string());
      progressCtx.setProgress(progressBarIndex, static_cast<float>(progress));
    }

    spdlog::debug(
      "Packing progress: {}%, File: {} -> {}",
      progress,
      entry.sourcePath.string(),
      entry.zipEntryName
    );
  }

  progressCtx.setProgress(progressBarIndex, 100.0f);
  progressCtx.setTone(progressBarIndex, progress::Tone::Finalizing);

  std::atomic<bool> finalizing{true};
  auto spinnerThread = std::jthread([&](std::stop_token stopToken) {
    using namespace std::chrono_literals;
    auto const frames = std::array{'|', '/', '-', '\\'};
    auto frameIndex = std::size_t{0};
    while (!stopToken.stop_requested() && finalizing.load(std::memory_order_acquire)) {
      progressCtx.setPostfixText(
        progressBarIndex,
        std::format("{} | Finalizing {}", progressText, frames[frameIndex])
      );
      frameIndex = (frameIndex + 1) % frames.size();
      std::this_thread::sleep_for(120ms);
    }
  });

  zip.close();

  finalizing.store(false, std::memory_order_release);
  spinnerThread.join();
  progressCtx.setTone(progressBarIndex, progress::Tone::Success);
  progressCtx.setPostfixText(progressBarIndex, progressText);

  return {};
} catch (std::exception const& e) {

  return eh::makeError(
    "Exception while packing files to zip {}: {}",
    zipFilePath.string(),
    e.what()
  );
}

auto groupFilesBySize(
  std::vector<fs::path> const& filePaths,
  std::uintmax_t maxGroupSize,
  std::optional<std::size_t> maxFilesPerGroup
) -> std::vector<std::vector<fs::path>> {
  auto preparedEntries = std::vector<PreparedPackEntry>{};
  preparedEntries.reserve(filePaths.size());
  for (auto const& filePath: filePaths) {
    preparedEntries.emplace_back(
      PreparedPackEntry{
        .entry = pack::PackFileEntry{.sourcePath = filePath, .zipEntryName = {}},
        .sourceKey = {},
        .fileKey = {},
        .fileSize = fs::file_size(filePath),
      }
    );
  }

  return sourcePathGroups(
    groupPreparedEntriesSequentially(preparedEntries, maxGroupSize, maxFilesPerGroup)
  );
}

auto groupPackEntries(
  std::vector<PackEntryInput> const& entries,
  std::uintmax_t maxGroupSize,
  std::optional<std::size_t> maxFilesPerGroup,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed
) -> std::vector<std::vector<pack::PackFileEntry>> {
  if (entries.empty()) { return {}; }

  auto preparedEntries = std::vector<PreparedPackEntry>{};
  preparedEntries.reserve(entries.size());
  for (auto const& input: entries) {
    preparedEntries.emplace_back(
      PreparedPackEntry{
        .entry = input.entry,
        .sourceKey = input.sourceKey.value_or(naming::stablePathString(input.sourceDir)),
        .fileKey = input.fileKey.value_or(buildPackEntryStableKey(input.entry)),
        .fileSize = fs::file_size(input.entry.sourcePath),
      }
    );
  }

  return groupPreparedEntries(
    std::move(preparedEntries),
    maxGroupSize,
    maxFilesPerGroup,
    keepSourceDirsTogetherWhenTotalFilesExceed
  );
}

auto groupPackFiles(
  std::vector<PackGroupInput> const& filePaths,
  std::uintmax_t maxGroupSize,
  std::optional<std::size_t> maxFilesPerGroup,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed
) -> std::vector<std::vector<fs::path>> {
  auto packEntries = std::vector<PackEntryInput>{};
  packEntries.reserve(filePaths.size());
  for (auto const& file: filePaths) {
    packEntries.emplace_back(
      PackEntryInput{
        .entry = pack::PackFileEntry{.sourcePath = file.filePath, .zipEntryName = {}},
        .sourceDir = file.sourceDir,
        .sourceKey = naming::stablePathString(file.sourceDir),
        .fileKey = naming::stablePathString(file.filePath),
      }
    );
  }

  return sourcePathGroups(groupPackEntries(
    packEntries,
    maxGroupSize,
    maxFilesPerGroup,
    keepSourceDirsTogetherWhenTotalFilesExceed
  ));
}

auto groupPackEntriesWithSubparts(
  std::vector<PackEntryInput> const& entries,
  std::uintmax_t maxGroupSize,
  std::size_t maxFilesPerPart,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed
) -> std::vector<PackEntryPartition> {
  if (entries.empty()) { return {}; }

  struct EntryMetadata {
    fs::path sourceDir;
    std::optional<std::string> sourceKey;
    std::optional<std::string> fileKey;
  };

  auto totalInputSize = std::uintmax_t{0};
  auto metadataByEntryKey = std::unordered_map<std::string, EntryMetadata>{};
  metadataByEntryKey.reserve(entries.size());
  for (auto const& input: entries) {
    totalInputSize += fs::file_size(input.entry.sourcePath);
    metadataByEntryKey.emplace(
      buildPackEntryStableKey(input.entry),
      EntryMetadata{
        .sourceDir = input.sourceDir,
        .sourceKey = input.sourceKey,
        .fileKey = input.fileKey,
      }
    );
  }

  auto const logicalParts = groupPackEntries(
    entries,
    totalInputSize,
    maxFilesPerPart,
    keepSourceDirsTogetherWhenTotalFilesExceed
  );
  auto const forceSourceCarryOverWithinPart =
    keepSourceDirsTogetherWhenTotalFilesExceed.has_value()
    && entries.size() > keepSourceDirsTogetherWhenTotalFilesExceed.value();

  auto groupedPartitions = std::vector<PackEntryPartition>{};
  groupedPartitions.reserve(logicalParts.size());

  for (auto partIndex = std::size_t{0}; partIndex < logicalParts.size(); ++partIndex) {
    auto subgroupInputs = std::vector<PackEntryInput>{};
    subgroupInputs.reserve(logicalParts[partIndex].size());

    for (auto const& entry: logicalParts[partIndex]) {
      auto const entryKey = buildPackEntryStableKey(entry);
      auto const metadataIt = metadataByEntryKey.find(entryKey);
      subgroupInputs.emplace_back(
        PackEntryInput{
          .entry = entry,
          .sourceDir = metadataIt != metadataByEntryKey.end()
            ? metadataIt->second.sourceDir
            : entry.sourcePath.parent_path(),
          .sourceKey = metadataIt != metadataByEntryKey.end()
            ? metadataIt->second.sourceKey
            : std::optional<std::string>{},
          .fileKey = metadataIt != metadataByEntryKey.end()
            ? metadataIt->second.fileKey
            : std::optional<std::string>{},
        }
      );
    }

    auto physicalGroups = groupPackEntries(
      subgroupInputs,
      maxGroupSize,
      std::nullopt,
      forceSourceCarryOverWithinPart ? std::optional<std::size_t>{0} : std::nullopt
    );

    for (auto subPartIndex = std::size_t{0}; subPartIndex < physicalGroups.size();
         ++subPartIndex) {
      groupedPartitions.emplace_back(
        PackEntryPartition{
          .entries = std::move(physicalGroups[subPartIndex]),
          .partIndex = partIndex + 1,
          .subPartIndex = subPartIndex,
        }
      );
    }
  }

  return groupedPartitions;
}

auto groupPackFilesWithSubparts(
  std::vector<PackGroupInput> const& filePaths,
  std::uintmax_t maxGroupSize,
  std::size_t maxFilesPerPart,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed
) -> std::vector<PackGroupPartition> {
  auto packEntries = std::vector<PackEntryInput>{};
  packEntries.reserve(filePaths.size());
  for (auto const& file: filePaths) {
    packEntries.emplace_back(
      PackEntryInput{
        .entry = pack::PackFileEntry{.sourcePath = file.filePath, .zipEntryName = {}},
        .sourceDir = file.sourceDir,
        .sourceKey = naming::stablePathString(file.sourceDir),
        .fileKey = naming::stablePathString(file.filePath),
      }
    );
  }

  auto const groupedEntryPartitions = groupPackEntriesWithSubparts(
    packEntries,
    maxGroupSize,
    maxFilesPerPart,
    keepSourceDirsTogetherWhenTotalFilesExceed
  );

  auto groupedPartitions = std::vector<PackGroupPartition>{};
  groupedPartitions.reserve(groupedEntryPartitions.size());
  for (auto const& partition: groupedEntryPartitions) {
    groupedPartitions.emplace_back(
      PackGroupPartition{
        .filePaths = sourcePathsForGroup(partition.entries),
        .partIndex = partition.partIndex,
        .subPartIndex = partition.subPartIndex,
      }
    );
  }

  return groupedPartitions;
}

auto packAllFilesInDirectory(
  fs::path const& dirPath,
  fs::path const& zipFileDir,
  std::uintmax_t maxGroupSize,
  bool recursive,
  bool forceNameConflictHandling,
  std::optional<std::size_t> maxParallelJobs
) -> eh::Result<void> {
  auto const planRes = buildDirectoryPackPlan(
    dirPath,
    zipFileDir,
    maxGroupSize,
    recursive,
    forceNameConflictHandling,
    maxParallelJobs
  );
  if (!planRes) { return eh::makeError("{}", planRes.error()); }

  auto const packRes = pack::packGroupsParallel(planRes.value());
  if (!packRes) { return eh::makeError("{}", packRes.error()); }

  return {};
}

auto buildDirectoryPackPlan(
  fs::path const& dirPath,
  fs::path const& zipFileDir,
  std::uintmax_t maxGroupSize,
  bool recursive,
  bool forceNameConflictHandling,
  std::optional<std::size_t> maxParallelJobs,
  std::optional<fs::path> excludedPath
) -> eh::Result<pack::PackPlan> {
  if (!fs::is_directory(dirPath)) {
    return eh::makeError("Input path is not a directory: {}", dirPath.string());
  }

  auto allFiles = std::vector<fs::path>{};

  terminal::println(
    Info,
    "Scanning input path for files: {} (recursive={})...",
    terminal::path(dirPath),
    recursive ? terminal::value("true") : terminal::value("false")
  );

  if (recursive) {
    for (auto const& entry: fs::recursive_directory_iterator(dirPath)) {
      if (!entry.is_regular_file()) { continue; }
      if (
        excludedPath.has_value()
        && entry.path().lexically_normal() == excludedPath->lexically_normal()
      ) {
        continue;
      }
      allFiles.emplace_back(entry.path());
    }
  } else {
    for (auto const& entry: fs::directory_iterator(dirPath)) {
      if (!entry.is_regular_file()) { continue; }
      if (
        excludedPath.has_value()
        && entry.path().lexically_normal() == excludedPath->lexically_normal()
      ) {
        continue;
      }
      allFiles.emplace_back(entry.path());
    }
  }

  if (allFiles.empty()) {
    return eh::makeError("No files found to pack in directory: {}", dirPath.string());
  }

  terminal::println(
    Info,
    "File scan completed, found {} file(s).",
    terminal::count(allFiles.size())
  );

  auto const groupedFiles = groupFilesBySize(allFiles, maxGroupSize);
  auto const ordinalRanges = pack::buildGroupOrdinalRanges(groupedFiles);
  auto groupedEntries = std::vector<std::vector<pack::PackFileEntry>>{};
  groupedEntries.reserve(groupedFiles.size());
  for (auto const& group: groupedFiles) {
    auto entries = std::vector<pack::PackFileEntry>{};
    entries.reserve(group.size());
    for (auto const& filePath: group) {
      entries.emplace_back(
        pack::PackFileEntry{
          .sourcePath = filePath,
          .zipEntryName = forceNameConflictHandling
            ? buildConflictHandledPackEntryName(dirPath, filePath)
            : filePath.filename().generic_string(),
        }
      );
    }
    groupedEntries.push_back(std::move(entries));
  }

  return pack::PackPlan{
    .groups = std::move(groupedEntries),
    .outputDir = zipFileDir,
    .zipNameForIndex =
      [dirName = dirPath.filename().string(), ordinalRanges](std::size_t index) {
        return pack::appendOrdinalRangeSuffix(
          std::format("{}_part{}.zip", dirName, index + 1),
          ordinalRanges.at(index)
        );
      },
    .maxParallelJobs = maxParallelJobs
  };
}
