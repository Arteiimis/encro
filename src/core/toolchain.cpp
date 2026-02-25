#include "core/toolchain.h"

#include "utils/utils.h"

#include <spdlog/spdlog.h>

namespace toolchain {

auto resolve(appctx::AppConfig const& config, appctx::ToolchainPaths& out)
  -> eh::Result<void> {
  out.ffmpegPath = findFFmpeg(config.ffmpegInstallDir);
  if (!out.ffmpegPath.has_value()) {
    return eh::makeError(
      "FFmpeg not found. Please ensure FFmpeg is installed and accessible."
    );
  }

  out.ffprobePath = findFFprobe(config.ffmpegInstallDir);
  if (!out.ffprobePath.has_value()) {
    return eh::makeError(
      "FFprobe not found. Please ensure FFprobe is installed and accessible."
    );
  }

  spdlog::info("Using FFmpeg at: {}", out.ffmpegPath.value().string());
  spdlog::info("Using FFprobe at: {}", out.ffprobePath.value().string());

  return {};
}

}  // namespace toolchain
