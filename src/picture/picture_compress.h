#pragma once

#include "core/app_context.h"
#include "core/media_item.h"
#include "picture/picture_types.h"

#include <filesystem>
#include <span>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

struct ImageCompressConfig {
  std::optional<fs::path> ffmpegPath = "ffmpeg";
  fs::path inputPath;
  fs::path outputPath;
  int quality = 2;

  auto buildCMD() const -> std::string;
};

// Temp path keeps the target media extension (<stem>.partial.<ext>) so the
// encoder infers the container; renamed atomically to outputPath on success.
// Shared by the picture workflow's compression and its video conversion.
auto partialTempPath(fs::path const& outputPath) -> fs::path;

// Removes any existing output and renames the partial over it, returning the
// rename's error code (cleared on success). The helper owns no policy: the
// callers keep their own existence guard, warning text and failure handling.
auto finalizePartialOutput(fs::path const& outputPath) -> std::error_code;

bool compressImage(
  appctx::AppContext const& ctx,
  fs::path const& inputPath,
  fs::path const& outputPath,
  int quality,
  std::string* failureReason = nullptr
);

// The compress stage over the flow's items: one JPEG artifact per item. The
// flow's mtime check built the item list, so the stage filters nothing; each
// item's outcome carries its failure reason.
auto compressImageBatch(
  appctx::AppContext& ctx,
  std::span<MediaItem> items,
  int quality,
  std::size_t maxParallel
) -> mediaitem::StageResult;
