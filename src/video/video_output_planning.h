#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <filesystem>
#include <optional>
#include <span>

namespace fs = std::filesystem;

struct EncodedVideoPackFile {
  fs::path outputPath;
};

auto planVideoOutputFiles(
  appctx::AppConfig const& config,
  std::span<fs::path const> inputPaths,
  std::optional<fs::path> const& sourceRootDir = std::nullopt
) -> eh::Result<appctx::path_map<fs::path>>;

auto resolveVideoPackOutputPath(
  appctx::AppConfig const& config,
  fs::path const& inputPath
) -> fs::path;

// The output root for planned encode outputs: the explicit --output when set,
// the webp default subdirectory next to the source root, or nullopt when
// outputs land next to their inputs.
auto resolveOutputRootDir(
  appctx::AppConfig const& config,
  std::optional<fs::path> const& sourceRootDir
) -> std::optional<fs::path>;
