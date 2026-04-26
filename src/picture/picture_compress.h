#pragma once

#include "core/app_context.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct ImageCompressConfig {
  std::optional<fs::path> ffmpegPath = "ffmpeg";
  fs::path inputPath;
  fs::path outputPath;
  int quality = 5;

  auto buildCMD() const -> std::string;
};

struct CompressTask {
  fs::path inputPath;
  fs::path outputPath;
  std::string entryName;
};

struct CompressResult {
  fs::path originalPath;
  fs::path compressedPath;
  std::string entryName;
};

auto compressImage(
  appctx::AppContext const& ctx,
  fs::path const& inputPath,
  fs::path const& outputPath,
  int quality
) -> bool;

auto compressImageBatch(
  appctx::AppContext& ctx,
  std::span<CompressTask const> tasks,
  int quality,
  std::size_t maxParallel
) -> std::vector<CompressResult>;
