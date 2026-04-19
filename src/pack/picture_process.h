#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"
#include "pack/pack_service.h"

#include <filesystem>
#include <span>
#include <vector>

auto readAllPics(appctx::AppConfig const& config, std::filesystem::path const& dirPath)
  -> std::vector<std::filesystem::path>;

auto packAllPicsToZipParallel(
  appctx::AppConfig const& config,
  std::filesystem::path const& dirPath,
  std::filesystem::path const& zipFileDir
) -> eh::Result<void>;

auto buildPicturePackPlan(
  appctx::AppConfig const& config,
  std::filesystem::path const& dirPath,
  std::filesystem::path const& zipFileDir
) -> eh::Result<pack::PackPlan>;

auto buildPicturePackPlan(
  appctx::AppConfig const& config,
  std::filesystem::path const& dirPath,
  std::filesystem::path const& zipFileDir,
  std::span<std::filesystem::path const> scannedPics
) -> eh::Result<pack::PackPlan>;
