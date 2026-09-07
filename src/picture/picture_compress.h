#pragma once

#include "core/app_context.h"

#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct ImageCompressConfig {
  std::optional<fs::path> ffmpegPath = "ffmpeg";
  fs::path inputPath;
  fs::path outputPath;
  int quality = 2;

  auto buildCMD() const -> std::string;
};

struct CompressTask {
  fs::path inputPath;
  fs::path outputPath;
  std::string entryName;
  std::string originalEntryName;
};

struct CompressResult {
  fs::path originalPath;
  fs::path compressedPath;
  std::string entryName;
  std::string originalEntryName;
};

// Temp path keeps the target media extension (<stem>.partial.<ext>) so the
// encoder infers the container; renamed atomically to outputPath on success.
auto compressionTempPath(fs::path const& outputPath) -> fs::path;

bool compressImage(
  appctx::AppContext const& ctx,
  fs::path const& inputPath,
  fs::path const& outputPath,
  int quality,
  std::string* failureReason = nullptr
);

auto compressImageBatch(
  appctx::AppContext& ctx,
  std::span<CompressTask const> tasks,
  int quality,
  std::size_t maxParallel,
  std::map<fs::path, std::string>& failureReasons
) -> std::vector<CompressResult>;
