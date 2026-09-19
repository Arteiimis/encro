#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <boost/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::literals;

namespace videoinfo {

// Video input extensions both workflows recognize. Single source for the video
// scan and for the picture run's conversion scan.
inline constexpr auto kVideoExtensions = std::array{
  ".mp4"sv,
  ".mkv"sv,
  ".avi"sv,
  ".mov"sv,
  ".flv"sv,
  ".wmv"sv,
};

// WebP input size limit: inputs at or above it are not converted.
inline constexpr std::uintmax_t kWebpInputMaxSize = 32ULL * 1024ULL * 1024ULL;

// Probe-free scan for the picture run's video→WebP conversion: the video
// extension set plus the WebP input size limit, with no ffprobe and no codec
// filtering (an already-HEVC clip still converts). A file past the size limit
// is skipped with a user-visible warning.
auto scanVideosForConversion(std::filesystem::path const& dirPath, bool recursive)
  -> eh::Result<std::vector<std::filesystem::path>>;

// Whole-json read of the process-scoped video info cache: the cached value
// when present, otherwise one ffprobe whose result is cached. This module owns
// the cache; other modules read metadata through this function.
auto cachedVidInfo(
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  std::filesystem::path const& videoPath
) -> boost::json::value;

}  // namespace videoinfo

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

auto getVidDimensions(
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  std::filesystem::path const& videoPath
) -> eh::Result<std::pair<int, int>>;

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
) -> eh::Result<std::vector<std::filesystem::path>>;

auto readAllVidsFromFiles(
  appctx::AppConfig const& config,
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  std::span<std::filesystem::path const> filePaths
) -> std::vector<std::filesystem::path>;
