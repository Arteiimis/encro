#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"
#include "picture/picture_video_webp.h"

#include <filesystem>
#include <vector>

auto readAllPics(appctx::AppConfig const& config, std::filesystem::path const& dirPath)
  -> eh::Result<std::vector<std::filesystem::path>>;

// Scans the input for clips and plans their conversions when the run converts
// videos: the archive entry name per the picture naming scheme, the cached
// WebP path, and the conversion cache's lifecycle for this run. Empty when the
// flag is off or the input holds no videos.
auto planPictureVideoConversions(
  appctx::AppContext& ctx,
  std::filesystem::path const& dirPath
) -> eh::Result<std::vector<picturewebp::ConversionTask>>;

auto runPicturePackWorkflow(appctx::AppContext& ctx, std::filesystem::path const& dirPath)
  -> eh::Result<int>;
