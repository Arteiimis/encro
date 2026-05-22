#include "video/video_encode_runner.h"

#include "video/video_progress_parser.h"

#include "core/display_text.h"
#include "infra/stop_signal.h"
#include "utils/utils.h"
#include "video/encode_config.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <cstdint>
#include <format>

DEFINE_LOGGER(logtags::VIDEO_ENCODE);

namespace fs = std::filesystem;

namespace {

constexpr auto kWebpTargetMaxSize = std::uintmax_t{20ULL * 1024ULL * 1024ULL};
constexpr auto kWebpMinQuality = 20;
constexpr auto kWebpQualityStep = 10;
constexpr auto kWebpFineQualityStep = 5;
constexpr auto kWebpSmallGapThreshold = std::uintmax_t{3ULL * 1024ULL * 1024ULL};

struct WebpEncodeContext {
  fs::path inputVidPath;
  fs::path outputFilePath;
  fs::path progressFilePath;
  std::function<void(std::string const&)> statusUpdater;
};

struct WebpEncodeStep {
  int exitCode;
  std::optional<std::uintmax_t> outputSize;
};

struct EncodeExecutionPlan {
  fs::path progressFilePath;
  std::optional<fs::path> outputPath;
  fs::path outputFilePath;
};

auto truncateEncodingStatus(std::string const& text, std::size_t maxLen = 72)
  -> std::string {
  return displaytext::truncateWithEllipsis(text, maxLen);
}

auto failEncoding(appctx::EncodingState& state, std::string const& error) -> bool {
  {
    auto lock = std::scoped_lock{state.mtx};
    state.lastError = error;
  }
  LOG_ERROR("{}", error);
  return false;
}

auto prepareEncodeExecution(appctx::EncodingState& state)
  -> eh::Result<EncodeExecutionPlan> {
  auto progressFilePath = fs::path{};
  auto outputPath = std::optional<fs::path>{};
  auto plannedOutputFile = std::optional<fs::path>{};
  {
    auto lock = std::scoped_lock{state.mtx};
    if (!state.progressFilePath.has_value()) {
      state.progressFilePath =
        fs::temp_directory_path() / std::format("progress_{}.txt", getUUID());
    }
    progressFilePath = state.progressFilePath.value();
    outputPath = state.outputPath;
    plannedOutputFile = state.plannedOutputFile;
  }

  if (!plannedOutputFile.has_value()) {
    return eh::makeError(
      "Failed to resolve output file for input: {}",
      state.inputPath.string()
    );
  }

  {
    auto ec = std::error_code{};
    fs::remove(progressFilePath, ec);
  }

  fs::create_directories(plannedOutputFile->parent_path());

  return EncodeExecutionPlan{
    .progressFilePath = progressFilePath,
    .outputPath = outputPath,
    .outputFilePath = plannedOutputFile.value(),
  };
}

auto buildEncodeConfig(
  appctx::AppContext const& ctx,
  appctx::EncodingState const& state,
  EncodeExecutionPlan const& plan
) -> EncodeConfig {
  return EncodeConfig{
    .ffmpegPath = ctx.toolchain.ffmpegPath,
    .inputPath = state.inputPath,
    .outputPath = plan.outputPath,
    .outputFilePath = plan.outputFilePath,
    .outputFormat = ctx.config.outputFormat,
    .progressFilePath = plan.progressFilePath
  };
}

void reportEncodingDiagnostic(function_ref statusUpdater, std::string_view line) {
  if (!statusUpdater || !isLikelyFfmpegErrorLine(line)) { return; }
  statusUpdater(truncateEncodingStatus(std::string{line}));
}

void clearWebpStaleFiles(fs::path const& progressFilePath, fs::path const& outputFile) {
  auto ec = std::error_code{};
  if (fs::exists(progressFilePath, ec)) { fs::remove(progressFilePath, ec); }
  if (fs::exists(outputFile, ec)) { fs::remove(outputFile, ec); }
}

auto runWebpEncodingStep(
  appctx::AppContext const& appCtx,
  WebpEncodeContext const& encodeCtx,
  uint8_t quality,
  fs::path const& outputFile
) -> WebpEncodeStep {
  clearWebpStaleFiles(encodeCtx.progressFilePath, outputFile);

  LOG_DEBUG(
    "WebP encoding step: input={} quality={} output={}",
    encodeCtx.inputVidPath.string(),
    quality,
    outputFile.string()
  );

  auto const cfg = EncodeConfig{
    .ffmpegPath = appCtx.toolchain.ffmpegPath,
    .inputPath = encodeCtx.inputVidPath,
    .outputFilePath = outputFile,
    .outputFormat = appCtx.config.outputFormat,
    .webpQuality = quality,
    .progressFilePath = encodeCtx.progressFilePath
  };

  if (auto const res = cfg.validate(); !res) {
    LOG_ERROR("{}", res.error());
    return {-1, std::nullopt};
  }

  auto const [exitCode, _] = exec2(cfg.buildCMD(), [&](std::string_view line) {
    reportEncodingDiagnostic(encodeCtx.statusUpdater, line);
  });
  if (exitCode != 0) {
    LOG_WARN(
      "WebP encoding step failed: input={} quality={} exitCode={}",
      encodeCtx.inputVidPath.string(),
      quality,
      exitCode
    );
    return {exitCode, std::nullopt};
  }
  if (!fs::exists(outputFile)) { return {exitCode, std::nullopt}; }

  LOG_DEBUG(
    "WebP encoding step output size: input={} quality={} bytes={}",
    encodeCtx.inputVidPath.string(),
    quality,
    fs::file_size(outputFile)
  );

  return {exitCode, fs::file_size(outputFile)};
}

auto encodeWebpWithTargetSize(
  appctx::AppContext const& appCtx,
  WebpEncodeContext const& encodeCtx
) -> bool {
  auto const outputFile = encodeCtx.outputFilePath;

  auto const abortForStopRequest = [&] {
    clearWebpStaleFiles(encodeCtx.progressFilePath, outputFile);
    LOG_INFO(
      "WebP adaptive encoding canceled: input={} output={}",
      encodeCtx.inputVidPath.string(),
      outputFile.string()
    );
    return false;
  };

  LOG_DEBUG(
    "WebP adaptive encoding start: input={} output={} target={} bytes",
    encodeCtx.inputVidPath.string(),
    outputFile.string(),
    kWebpTargetMaxSize
  );

  auto const qualityStepForSize = [](std::uintmax_t outputSize) {
    auto const sizeGap = outputSize - kWebpTargetMaxSize;
    return sizeGap <= kWebpSmallGapThreshold ? kWebpFineQualityStep : kWebpQualityStep;
  };

  auto quality = 80u;
  while (quality >= kWebpMinQuality) {
    if (stopsignal::isStopRequested()) { return abortForStopRequest(); }

    if (encodeCtx.statusUpdater) {
      encodeCtx.statusUpdater(std::format("q={}", quality));
    }
    auto const stepRes = runWebpEncodingStep(appCtx, encodeCtx, quality, outputFile);
    if (
      stopsignal::isStopRequested() || stepRes.exitCode == stopsignal::kCanceledExitCode
    ) {
      return abortForStopRequest();
    }
    if (stepRes.exitCode != 0) {
      LOG_ERROR(
        "WebP encoding step failed permanently: input={} quality={} exitCode={}",
        encodeCtx.inputVidPath.string(),
        quality,
        stepRes.exitCode
      );
      return false;
    }
    if (!stepRes.outputSize.has_value()) {
      LOG_ERROR(
        "WebP encoding step produced no output file: input={} quality={} output={}",
        encodeCtx.inputVidPath.string(),
        quality,
        outputFile.string()
      );
      return false;
    }

    auto const outputSize = stepRes.outputSize.value();
    if (outputSize < kWebpTargetMaxSize) {
      LOG_DEBUG(
        "WebP encoded under target size: {} ({} bytes, q={})",
        outputFile.string(),
        outputSize,
        quality
      );
      return true;
    }

    auto const step = qualityStepForSize(outputSize);
    auto const nextQuality = quality - step;
    if (encodeCtx.statusUpdater && nextQuality >= kWebpMinQuality) {
      auto const outputSizeMB = static_cast<double>(outputSize) / 1024.0 / 1024.0;
      encodeCtx
        .statusUpdater(std::format("retry q={} ({:.1f}MB)", nextQuality, outputSizeMB));
    }

    quality -= step;
  }

  if (fs::exists(outputFile)) {
    LOG_WARN(
      "WebP encoding reached minimum quality but still over target: input={} "
      "output={} bytes={}",
      encodeCtx.inputVidPath.string(),
      outputFile.string(),
      fs::file_size(outputFile)
    );
    if (encodeCtx.statusUpdater) {
      auto const outputSizeMB =
        static_cast<double>(fs::file_size(outputFile)) / 1024.0 / 1024.0;
      encodeCtx.statusUpdater(std::format("min-q reached ({:.1f}MB)", outputSizeMB));
    }
    return true;
  }

  LOG_ERROR(
    "WebP adaptive encoding failed: input={} output={}",
    encodeCtx.inputVidPath.string(),
    outputFile.string()
  );

  return false;
}

auto runStandardEncoding(
  appctx::EncodingState const& state,
  EncodeConfig const& cfg,
  function_ref statusUpdater
) -> bool {
  auto const [exitCode, _] = exec2(cfg.buildCMD(), [&](std::string_view line) {
    reportEncodingDiagnostic(statusUpdater, line);
  });
  if (exitCode != 0) {
    LOG_WARN(
      "ffmpeg exited with non-zero code: input={} exitCode={}",
      state.inputPath.string(),
      exitCode
    );
  }

  return exitCode == 0;
}

}  // namespace

bool encodeVideo(
  appctx::AppContext& ctx,
  appctx::EncodingState& state,
  function_ref statusUpdater
) {
  auto const executionPlanRes = prepareEncodeExecution(state);
  if (!executionPlanRes) { return failEncoding(state, executionPlanRes.error()); }

  auto const& executionPlan = executionPlanRes.value();
  auto const cfg = buildEncodeConfig(ctx, state, executionPlan);

  LOG_DEBUG(
    "Encode config: input={} output-format={} output-file={} progress-file={}",
    state.inputPath.string(),
    ctx.config.outputFormat,
    executionPlan.outputFilePath.string(),
    executionPlan.progressFilePath.string()
  );

  auto const validationResult = cfg.validate();
  if (!validationResult) { return failEncoding(state, validationResult.error()); }

  LOG_DEBUG("Encoding video: {}", state.inputPath.string());

  if (ctx.config.outputFormat == "webp") {
    return encodeWebpWithTargetSize(
      ctx,
      WebpEncodeContext{
        .inputVidPath = state.inputPath,
        .outputFilePath = executionPlan.outputFilePath,
        .progressFilePath = executionPlan.progressFilePath,
        .statusUpdater = statusUpdater
      }
    );
  }

  return runStandardEncoding(state, cfg, statusUpdater);
}
