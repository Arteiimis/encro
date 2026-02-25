#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <boost/json.hpp>

#include <filesystem>
#include <vector>


auto getVidInfo(
  appctx::ToolchainPaths const& toolchain,
  std::filesystem::path const& videoPath
) -> boost::json::value;

auto getVidTotalFrames(
  appctx::RuntimeContext const& runtime,
  std::filesystem::path const& videoPath
) -> eh::Result<int64_t>;

bool isHevcEncoded(
  appctx::ToolchainPaths const& toolchain,
  std::filesystem::path const& videoPath
);

auto readAllVids(
  appctx::AppConfig const& config,
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  std::filesystem::path const& dirPath
) -> std::vector<std::filesystem::path>;
