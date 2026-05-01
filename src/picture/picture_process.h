#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <filesystem>
#include <vector>

auto readAllPics(appctx::AppConfig const& config, std::filesystem::path const& dirPath)
  -> std::vector<std::filesystem::path>;

auto runPicturePackWorkflow(appctx::AppContext& ctx, std::filesystem::path const& dirPath)
  -> eh::Result<int>;

auto packAllPicsToZip(
  appctx::AppConfig const& config,
  std::filesystem::path const& dirPath,
  std::filesystem::path const& zipFileDir
) -> eh::Result<void>;
