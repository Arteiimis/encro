#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <filesystem>
#include <vector>

auto readAllPics(
  appctx::AppConfig const& config,
  std::filesystem::path const& dirPath
) -> std::vector<std::filesystem::path>;

auto packAllPicsToZipParallel(
  appctx::AppConfig const& config,
  std::filesystem::path const& dirPath,
  std::filesystem::path const& zipFileDir
) -> eh::Result<void>;
