#include "pack/packer.h"

#include "core/collision_naming.h"
#include "core/progress.h"
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
#include <print>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <stop_token>
#include <thread>

namespace fs = std::filesystem;
using namespace indicators;
namespace naming = collisionnaming;

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

struct PreparedPackFile {
  fs::path filePath;
  std::string sourceKey;
  std::string fileKey;
  std::uintmax_t fileSize = 0;
};

struct PreparedPackChunk {
  std::vector<fs::path> filePaths;
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

void flushGroupedFiles(
  std::vector<fs::path>& currentGroup,
  std::uintmax_t& currentSize,
  std::size_t& currentCount,
  std::vector<std::vector<fs::path>>& groupedFiles
);

auto groupPreparedFilesSequentially(
  std::vector<PreparedPackFile> const& preparedFiles,
  std::uintmax_t maxGroupSize,
  std::optional<std::size_t> maxFilesPerGroup
) -> std::vector<std::vector<fs::path>> {
  auto groupedFiles = std::vector<std::vector<fs::path>>{};
  auto currentGroup = std::vector<fs::path>{};
  auto currentSize = std::uintmax_t{0};
  auto currentCount = std::size_t{0};

  for (auto const& file: preparedFiles) {
    if (
      !currentGroup.empty()
      && wouldExceedGroupLimits(
        currentSize,
        currentCount,
        file.fileSize,
        1,
        maxGroupSize,
        maxFilesPerGroup
      )
    ) {
      flushGroupedFiles(currentGroup, currentSize, currentCount, groupedFiles);
    }

    currentGroup.emplace_back(file.filePath);
    currentSize += file.fileSize;
    ++currentCount;
  }

  flushGroupedFiles(currentGroup, currentSize, currentCount, groupedFiles);
  return groupedFiles;
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
  if (currentChunk.filePaths.empty()) { return; }
  chunks.emplace_back(std::move(currentChunk));
  currentChunk = {};
}

void flushGroupedFiles(
  std::vector<fs::path>& currentGroup,
  std::uintmax_t& currentSize,
  std::size_t& currentCount,
  std::vector<std::vector<fs::path>>& groupedFiles
) {
  if (currentGroup.empty()) { return; }
  groupedFiles.emplace_back(std::move(currentGroup));
  currentGroup = {};
  currentSize = 0;
  currentCount = 0;
}

auto splitSourceDirectoryEntries(
  std::vector<PreparedPackFile> const& entries,
  std::uintmax_t maxGroupSize,
  std::optional<std::size_t> maxFilesPerGroup
) -> std::vector<PreparedPackChunk> {
  auto chunks = std::vector<PreparedPackChunk>{};
  auto currentChunk = PreparedPackChunk{};

  for (auto const& entry: entries) {
    if (
      !currentChunk.filePaths.empty()
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

    currentChunk.filePaths.emplace_back(entry.filePath);
    currentChunk.totalSize += entry.fileSize;
    ++currentChunk.fileCount;
  }

  flushPreparedChunk(currentChunk, chunks);
  return chunks;
}

}  // namespace

auto packFilesToZip(
  std::vector<fs::path> const& filePaths,
  fs::path const& zipFilePath,
  progress::ProgressContext& progressCtx,
  std::string_view progressText,
  ZipEntryNameResolver entryNameForFile
) -> eh::Result<void> try {
  auto zip = libzippp::ZipArchive(zipFilePath.string());
  auto fileCount = filePaths.size();
  auto const progressBarIndex = progressCtx.addBar(progressText);
  auto usedEntryNames = std::unordered_set<std::string>{};

  zip.open(libzippp::ZipArchive::New);

  for (auto const& [index, filePath]: std::views::enumerate(filePaths)) {
    auto const progress = (size_t)std::round((index + 1) / (float)fileCount * 100.0f);

    if (fs::is_regular_file(filePath)) {
      auto const preferredEntryName =
        entryNameForFile ? entryNameForFile(filePath) : filePath.filename().string();
      auto const entryName =
        makeUniqueZipEntryName(preferredEntryName, filePath, usedEntryNames);
      zip.addFile(entryName, filePath.string());
      progressCtx.setProgress(progressBarIndex, static_cast<float>(progress));
    }

    spdlog::debug("Packing progress: {}%, File: {}", progress, filePath.string());
  }

  progressCtx.setProgress(progressBarIndex, 100.0f);

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
  auto preparedFiles = std::vector<PreparedPackFile>{};
  preparedFiles.reserve(filePaths.size());
  for (auto const& filePath: filePaths) {
    preparedFiles.emplace_back(
      PreparedPackFile{
        .filePath = filePath,
        .sourceKey = {},
        .fileKey = {},
        .fileSize = fs::file_size(filePath),
      }
    );
  }

  return groupPreparedFilesSequentially(preparedFiles, maxGroupSize, maxFilesPerGroup);
}

auto groupPackFiles(
  std::vector<PackGroupInput> const& filePaths,
  std::uintmax_t maxGroupSize,
  std::optional<std::size_t> maxFilesPerGroup,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed
) -> std::vector<std::vector<fs::path>> {
  if (filePaths.empty()) { return {}; }

  auto preparedFiles = std::vector<PreparedPackFile>{};
  preparedFiles.reserve(filePaths.size());
  for (auto const& file: filePaths) {
    preparedFiles.emplace_back(
      PreparedPackFile{
        .filePath = file.filePath,
        .sourceKey = naming::stablePathString(file.sourceDir),
        .fileKey = naming::stablePathString(file.filePath),
        .fileSize = fs::file_size(file.filePath),
      }
    );
  }

  if (
    !keepSourceDirsTogetherWhenTotalFilesExceed.has_value()
    || filePaths.size() <= keepSourceDirsTogetherWhenTotalFilesExceed.value()
  ) {
    return groupPreparedFilesSequentially(preparedFiles, maxGroupSize, maxFilesPerGroup);
  }

  std::ranges::sort(
    preparedFiles,
    [](PreparedPackFile const& lhs, PreparedPackFile const& rhs) {
      if (lhs.sourceKey != rhs.sourceKey) { return lhs.sourceKey < rhs.sourceKey; }
      return lhs.fileKey < rhs.fileKey;
    }
  );

  auto groupedFiles = std::vector<std::vector<fs::path>>{};
  auto currentGroup = std::vector<fs::path>{};
  auto currentSize = std::uintmax_t{0};
  auto currentCount = std::size_t{0};

  auto currentSourceEntries = std::vector<PreparedPackFile>{};
  auto currentSourceKey = std::string{};

  auto packSourceEntries = [&](std::vector<PreparedPackFile> const& entries) {
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
        flushGroupedFiles(currentGroup, currentSize, currentCount, groupedFiles);
      }

      currentGroup
        .insert(currentGroup.end(), chunk.filePaths.begin(), chunk.filePaths.end());
      currentSize += chunk.totalSize;
      currentCount += chunk.fileCount;
    }
  };

  for (auto const& entry: preparedFiles) {
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
  flushGroupedFiles(currentGroup, currentSize, currentCount, groupedFiles);

  return groupedFiles;
}

auto groupPackFilesWithSubparts(
  std::vector<PackGroupInput> const& filePaths,
  std::uintmax_t maxGroupSize,
  std::size_t maxFilesPerPart,
  std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed
) -> std::vector<PackGroupPartition> {
  if (filePaths.empty()) { return {}; }

  auto totalInputSize = std::uintmax_t{0};
  auto sourceDirByFileKey = std::unordered_map<std::string, fs::path>{};
  sourceDirByFileKey.reserve(filePaths.size());
  for (auto const& file: filePaths) {
    totalInputSize += fs::file_size(file.filePath);
    sourceDirByFileKey.emplace(naming::stablePathString(file.filePath), file.sourceDir);
  }

  auto const logicalParts = groupPackFiles(
    filePaths,
    totalInputSize,
    maxFilesPerPart,
    keepSourceDirsTogetherWhenTotalFilesExceed
  );
  auto const forceSourceCarryOverWithinPart =
    keepSourceDirsTogetherWhenTotalFilesExceed.has_value()
    && filePaths.size() > keepSourceDirsTogetherWhenTotalFilesExceed.value();

  auto groupedPartitions = std::vector<PackGroupPartition>{};
  groupedPartitions.reserve(logicalParts.size());

  for (auto partIndex = std::size_t{0}; partIndex < logicalParts.size(); ++partIndex) {
    auto subgroupInputs = std::vector<PackGroupInput>{};
    subgroupInputs.reserve(logicalParts[partIndex].size());

    for (auto const& filePath: logicalParts[partIndex]) {
      auto const fileKey = naming::stablePathString(filePath);
      auto const sourceDirIt = sourceDirByFileKey.find(fileKey);
      subgroupInputs.emplace_back(
        PackGroupInput{
          filePath,
          sourceDirIt != sourceDirByFileKey.end() ? sourceDirIt->second
                                                  : filePath.parent_path()
        }
      );
    }

    auto physicalGroups = groupPackFiles(
      subgroupInputs,
      maxGroupSize,
      std::nullopt,
      forceSourceCarryOverWithinPart ? std::optional<std::size_t>{0} : std::nullopt
    );

    for (auto subPartIndex = std::size_t{0}; subPartIndex < physicalGroups.size();
         ++subPartIndex) {
      groupedPartitions.emplace_back(
        PackGroupPartition{
          .filePaths = std::move(physicalGroups[subPartIndex]),
          .partIndex = partIndex + 1,
          .subPartIndex = subPartIndex,
        }
      );
    }
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

  std::println(
    "Scanning input path for files: {} (recursive={})...",
    dirPath.string(),
    recursive ? "true" : "false"
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

  std::println("File scan completed, found {} file(s).", allFiles.size());

  auto const groupedFiles = groupFilesBySize(allFiles, maxGroupSize);
  auto const ordinalRanges = pack::buildGroupOrdinalRanges(groupedFiles);
  return pack::PackPlan{
    .groups = groupedFiles,
    .outputDir = zipFileDir,
    .zipNameForIndex =
      [dirName = dirPath.filename().string(), ordinalRanges](std::size_t index) {
        return pack::appendOrdinalRangeSuffix(
          std::format("{}_part{}.zip", dirName, index + 1),
          ordinalRanges.at(index)
        );
      },
    .zipEntryNameForFile = forceNameConflictHandling
      ? ZipEntryNameResolver{[dirPath](fs::path const& filePath) {
          return buildConflictHandledPackEntryName(dirPath, filePath);
        }}
      : ZipEntryNameResolver{},
    .maxParallelJobs = maxParallelJobs
  };
}
