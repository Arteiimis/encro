#pragma once

#include "core/app_context.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <vector>


namespace fs = std::filesystem;

using function_ref = std::function<void(std::string const&)> const&;
bool encodeToHevc(
  appctx::AppContext& ctx,
  fs::path const& inputVidPath,
  function_ref statusUpdater = {}
);

auto handleSingleFileEncoding(appctx::AppContext& ctx, fs::path const& videoPath)
  -> int;

auto readLastNLines(const fs::path& filePath, std::size_t n)
  -> std::vector<std::string>;

struct ProgressData {
  uint64_t frameCount;
  std::string status;
};

auto parseProgressFile(const fs::path& progressFilePath) -> ProgressData;

auto resolveVideoOutputPath(
  appctx::AppConfig const& config,
  fs::path const& inputPath
) -> std::optional<fs::path>;

auto resolveVideoPackOutputPath(
  appctx::AppConfig const& config,
  fs::path const& inputPath
) -> fs::path;

auto groupEncodedVideosForPack(std::vector<fs::path> const& filePaths)
  -> std::vector<std::vector<fs::path>>;

auto handlePathEncoding(appctx::AppContext& ctx, fs::path const& inputPath) -> int;
