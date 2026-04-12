#include "pack/packer.h"

#include "core/progress.h"
#include "pack/pack_service.h"

#include <libzippp/libzippp.h>
#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <print>
#include <ranges>
#include <unordered_set>
#include <stop_token>
#include <thread>

namespace fs = std::filesystem;
using namespace indicators;

namespace {

auto stablePathString(fs::path const& path) -> std::string {
  auto normalized = path.lexically_normal().generic_string();
  std::ranges::transform(normalized, normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return normalized;
}

auto fnv1a32(std::string_view text) -> std::uint32_t {
  auto hash = std::uint32_t{2166136261u};
  for (auto const ch: text) {
    hash ^= static_cast<unsigned char>(ch);
    hash *= 16777619u;
  }
  return hash;
}

auto shortPathHash(fs::path const& path) -> std::string {
  return std::format("{:08x}", fnv1a32(stablePathString(path)));
}

auto sanitizeLabel(std::string_view text) -> std::string {
  auto sanitized = std::string{};
  sanitized.reserve(text.size());

  auto lastWasSeparator = false;
  for (auto const ch: text) {
    if (std::isalnum(static_cast<unsigned char>(ch))) {
      sanitized.push_back(static_cast<char>(std::tolower(ch)));
      lastWasSeparator = false;
      continue;
    }

    if (!lastWasSeparator) {
      sanitized.push_back('_');
      lastWasSeparator = true;
    }
  }

  while (!sanitized.empty() && sanitized.front() == '_') {
    sanitized.erase(sanitized.begin());
  }
  while (!sanitized.empty() && sanitized.back() == '_') { sanitized.pop_back(); }

  return sanitized;
}

auto relativeParentPath(fs::path const& rootDir, fs::path const& filePath)
  -> std::optional<fs::path> {
  auto const relativePath = filePath.parent_path().lexically_relative(rootDir);
  if (relativePath.empty() || relativePath == fs::path{"."}) {
    return std::nullopt;
  }

  return relativePath;
}

auto buildPackCollisionGroupLabel(fs::path const& dirPath, fs::path const& filePath)
  -> std::string {
  auto label = std::string{};
  if (auto const relativePath = relativeParentPath(dirPath, filePath);
      relativePath.has_value()) {
    label = sanitizeLabel(relativePath->generic_string());
  }

  if (label.empty() && filePath.has_extension()) {
    auto const extension = filePath.extension().string();
    auto const extensionView =
      std::string_view{extension}.substr(extension.starts_with('.') ? 1 : 0);
    label = sanitizeLabel(extensionView);
  }

  if (label.empty()) { label = "src"; }

  return label;
}

auto buildConflictHandledPackEntryName(
  fs::path const& dirPath,
  fs::path const& filePath
) -> std::string {
  return std::format(
    "{}__{}__{}{}",
    buildPackCollisionGroupLabel(dirPath, filePath),
    filePath.stem().string(),
    shortPathHash(filePath),
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
  if (usedEntryNames.insert(normalizedEntryName).second) {
    return normalizedEntryName;
  }

  auto const entryPath = fs::path{normalizedEntryName};
  auto const suffix = std::format("__{}", shortPathHash(filePath));
  auto const parentPath = entryPath.parent_path();
  auto const stem = entryPath.stem().string();
  auto const extension = entryPath.extension().string();

  auto candidate = (parentPath / std::format("{}{}{}", stem, suffix, extension))
                     .generic_string();
  auto duplicateIndex = std::size_t{1};
  while (!usedEntryNames.insert(candidate).second) {
    candidate =
      (parentPath
       / std::format("{}{}_{}{}", stem, suffix, duplicateIndex++, extension))
          .generic_string();
  }

  return candidate;
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
    auto const progress =
      (size_t)std::round((index + 1) / (float)fileCount * 100.0f);

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
    while (!stopToken.stop_requested()
           && finalizing.load(std::memory_order_acquire)) {
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
  std::uintmax_t maxGroupSize
) -> std::vector<std::vector<fs::path>> {
  auto groupedFiles = std::vector<std::vector<fs::path>>{};
  auto currentGroup = std::vector<fs::path>{};
  auto currentSize = std::uintmax_t{0};

  for (auto const& filePath: filePaths) {
    auto const fileSize = fs::file_size(filePath);

    if (currentSize + fileSize > maxGroupSize && !currentGroup.empty()) {
      groupedFiles.emplace_back(currentGroup);
      currentGroup.clear();
      currentSize = 0;
    }

    currentGroup.emplace_back(filePath);
    currentSize += fileSize;
  }

  if (!currentGroup.empty()) { groupedFiles.emplace_back(currentGroup); }

  return groupedFiles;
}

auto packAllFilesInDirectory(
  fs::path const& dirPath,
  fs::path const& zipFileDir,
  std::uintmax_t maxGroupSize,
  bool recursive,
  bool forceNameConflictHandling,
  std::optional<std::size_t> maxParallelJobs
) -> eh::Result<void> {
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
      if (entry.is_regular_file()) { allFiles.emplace_back(entry.path()); }
    }
  } else {
    for (auto const& entry: fs::directory_iterator(dirPath)) {
      if (entry.is_regular_file()) { allFiles.emplace_back(entry.path()); }
    }
  }

  if (allFiles.empty()) {
    return eh::makeError(
      "No files found to pack in directory: {}",
      dirPath.string()
    );
  }

  std::println("File scan completed, found {} file(s).", allFiles.size());

  auto const groupedFiles = groupFilesBySize(allFiles, maxGroupSize);
  auto const plan = pack::PackPlan{
    .groups = groupedFiles,
    .outputDir = zipFileDir,
    .zipNameForIndex =
      [dirName = dirPath.filename().string()](std::size_t index) {
        return std::format("{}_part{}.zip", dirName, index + 1);
      },
    .progressLabelForIndex =
      [dirName = dirPath.filename().string()](std::size_t index) {
        return std::format("Packing: {}_part{}.zip", dirName, index + 1);
      },
    .zipEntryNameForFile =
      forceNameConflictHandling
        ? ZipEntryNameResolver{[dirPath](fs::path const& filePath) {
            return buildConflictHandledPackEntryName(dirPath, filePath);
          }}
        : ZipEntryNameResolver{},
    .maxParallelJobs = maxParallelJobs
  };

  auto const packRes = pack::packGroupsParallel(plan);
  if (!packRes) { return eh::makeError("{}", packRes.error()); }

  return {};
}
