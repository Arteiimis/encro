#include "video/video_output_planning.h"

#include "core/collision_naming.h"

#include "video/encode_config.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <format>
#include <ranges>

namespace fs = std::filesystem;
namespace naming = collisionnaming;

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::VIDEO_OUTPUT);

namespace {

bool shouldForceConflictNaming(appctx::AppConfig const& config) {
  return config.forceNameConflictHandling
    && config.outputLayout == appctx::OutputLayout::Flat
    && (config.outputFormat != "mp4" || config.packOutput);
}

auto buildConflictHandledOutputPath(
  std::optional<fs::path> const& sourceRootDir,
  fs::path const& inputPath,
  fs::path const& candidatePath
) -> fs::path {
  return  //
    candidatePath.parent_path()
    / naming::buildConflictHandledFlatName(
      sourceRootDir,
      inputPath,
      candidatePath.stem().string(),
      candidatePath.extension().string()
    );
}

auto resolveOutputRootDir(
  appctx::AppConfig const& config,
  std::optional<fs::path> const& sourceRootDir
) -> std::optional<fs::path> {
  if (config.outputPath.has_value()) { return *config.outputPath; }
  if (config.outputFormat != "webp" || !sourceRootDir.has_value()) {
    return std::nullopt;
  }

  return *sourceRootDir / "encoded_webp";
}

auto resolvePlannedOutputDir(
  appctx::AppConfig const& config,
  fs::path const& inputPath,
  std::optional<fs::path> const& sourceRootDir,
  std::optional<fs::path> const& outputRootDir
) -> fs::path {
  if (!outputRootDir.has_value()) { return inputPath.parent_path(); }

  auto outputDir = outputRootDir.value();
  if (config.outputLayout != appctx::OutputLayout::Keep) { return outputDir; }

  if (
    auto const relativePath = naming::relativeParentPath(sourceRootDir, inputPath);
    relativePath.has_value()
  ) {
    outputDir /= relativePath.value();
  }

  return outputDir;
}

void ensureUniqueOutputPaths(appctx::path_map<fs::path>& plannedOutputFiles) {
  while (true) {
    auto duplicateGroups = appctx::path_map<std::vector<fs::path>>{};
    duplicateGroups.reserve(plannedOutputFiles.size());

    for (auto const& [inputPath, outputPath]: plannedOutputFiles) {
      duplicateGroups[outputPath].push_back(inputPath);
    }

    auto hadDuplicates = false;
    for (auto const& [outputPath, inputPaths]: duplicateGroups) {
      if (inputPaths.size() < 2) { continue; }

      hadDuplicates = true;
      auto const stem = outputPath.stem().string();
      auto const extension = outputPath.extension().string();
      for (auto const& inputPath: inputPaths) {
        plannedOutputFiles[inputPath] = outputPath.parent_path()
          / std::format("{}__{}{}", stem, naming::shortPathHash(inputPath), extension);
      }
    }

    if (!hadDuplicates) { return; }
  }
}

}  // namespace

auto planVideoOutputFiles(
  appctx::AppConfig const& config,
  std::span<fs::path const> inputPaths,
  std::optional<fs::path> const& sourceRootDir
) -> eh::Result<appctx::path_map<fs::path>> {
  auto plannedOutputFiles = appctx::path_map<fs::path>{};
  plannedOutputFiles.reserve(inputPaths.size());

  if (inputPaths.empty()) { return plannedOutputFiles; }

  auto const outputRootDir = resolveOutputRootDir(config, sourceRootDir);
  auto const usesSharedOutputRoot = outputRootDir.has_value();

  if (
    usesSharedOutputRoot
    && config.outputLayout == appctx::OutputLayout::Keep
    && !sourceRootDir.has_value()
  ) {
    return eh::makeError(
      "--keep requires input files to share a common parent directory."
    );
  }

  auto groupedCandidates = appctx::path_map<std::vector<fs::path>>{};
  groupedCandidates.reserve(inputPaths.size());
  auto const forceConflictNaming = shouldForceConflictNaming(config);

  for (auto const& inputPath: inputPaths) {
    auto const outputDir =
      resolvePlannedOutputDir(config, inputPath, sourceRootDir, outputRootDir);
    auto const fileName =
      EncodeConfig{.inputPath = inputPath, .outputFormat = config.outputFormat}
        .buildOutputFileName();
    groupedCandidates[outputDir / fileName].push_back(inputPath);
  }

  for (auto const& [candidatePath, groupedInputs]: groupedCandidates) {
    if (groupedInputs.size() == 1 && !forceConflictNaming) {
      plannedOutputFiles[groupedInputs.front()] = candidatePath;
      continue;
    }

    auto sortedInputs = groupedInputs;
    std::ranges::sort(sortedInputs, [](fs::path const& lhs, fs::path const& rhs) {
      return naming::stablePathString(lhs) < naming::stablePathString(rhs);
    });

    for (auto const& inputPath: sortedInputs) {
      plannedOutputFiles[inputPath] =
        buildConflictHandledOutputPath(sourceRootDir, inputPath, candidatePath);
    }
  }

  ensureUniqueOutputPaths(plannedOutputFiles);

  return plannedOutputFiles;
}

auto resolveVideoPackOutputPath(
  appctx::AppConfig const& config,
  fs::path const& inputPath
) -> fs::path {
  if (config.outputPath.has_value()) { return config.outputPath.value() / "packed"; }

  auto const basePath = fs::is_directory(inputPath) ? inputPath : inputPath.parent_path();
  return basePath / "packed";
}
