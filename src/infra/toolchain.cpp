#include "infra/toolchain.h"

#include "utils/utils.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

DEFINE_LOGGER(logtags::INFRA_TOOLCHAIN)

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

  LOG_INFO("Using FFmpeg at: {}", out.ffmpegPath.value().string());
  LOG_INFO("Using FFprobe at: {}", out.ffprobePath.value().string());

  return {};
}

}  // namespace toolchain
