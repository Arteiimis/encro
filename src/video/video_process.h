#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace fs = std::filesystem;

struct EncodedVideoPackFile {
  fs::path sourcePath;
  fs::path outputPath;
};

using function_ref = std::function<void(std::string const&)> const&;
bool encodeToHevc(
  appctx::AppContext& ctx,
  appctx::EncodingState& state,
  function_ref statusUpdater = {}
);

auto handleSingleFileEncoding(appctx::AppContext& ctx, fs::path const& videoPath) -> int;

auto handleMultiFileEncoding(
  appctx::AppContext& ctx,
  std::span<fs::path const> inputPaths
) -> int;

auto readLastNLines(fs::path const& filePath, std::size_t n) -> std::vector<std::string>;

struct ProgressData {
  uint64_t frameCount;
  std::string status;
};

auto parseProgressFile(fs::path const& progressFilePath) -> ProgressData;

auto planVideoOutputFiles(
  appctx::AppConfig const& config,
  std::span<fs::path const> inputPaths,
  std::optional<fs::path> sourceRootDir = std::nullopt
) -> eh::Result<appctx::path_map<fs::path>>;

auto resolveVideoOutputPath(appctx::AppConfig const& config, fs::path const& inputPath)
  -> std::optional<fs::path>;

auto resolveVideoPackOutputPath(
  appctx::AppConfig const& config,
  fs::path const& inputPath
) -> fs::path;

auto groupEncodedVideosForPack(std::vector<fs::path> const& filePaths)
  -> std::vector<std::vector<fs::path>>;

auto groupEncodedVideosForPack(
  std::vector<EncodedVideoPackFile> const& filePaths,
  std::size_t keepSourceDirsTogetherWhenTotalFilesExceed = 2000
) -> std::vector<std::vector<fs::path>>;

auto handlePathEncoding(appctx::AppContext& ctx, fs::path const& inputPath) -> int;
