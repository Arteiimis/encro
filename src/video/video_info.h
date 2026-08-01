#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <boost/json.hpp>

#include <filesystem>
#include <span>
#include <vector>

auto getVidInfo(
  appctx::ToolchainPaths const& toolchain,
  std::filesystem::path const& videoPath
) -> boost::json::value;

auto getVidTotalFrames(
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  std::filesystem::path const& videoPath
) -> eh::Result<int64_t>;

auto getVidTotalDurationUs(
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  std::filesystem::path const& videoPath
) -> eh::Result<std::uint64_t>;

auto getVidHasAudio(
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  std::filesystem::path const& videoPath
) -> eh::Result<bool>;

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

auto readAllVidsFromFiles(
  appctx::AppConfig const& config,
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  std::span<std::filesystem::path const> filePaths
) -> std::vector<std::filesystem::path>;
