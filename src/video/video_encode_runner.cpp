#include "video/video_encode_runner.h"

#include "video/video_info.h"
#include "video/video_progress_parser.h"

#include "core/display_text.h"
#include "core/job_state.h"
#include "infra/stop_signal.h"
#include "utils/utils.h"
#include "video/encode_config.h"
#include "video/segment_dir.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::VIDEO_ENCODE);

namespace fs = std::filesystem;

namespace {

constexpr auto kWebpTargetMaxSize = std::uintmax_t{20ULL * 1024ULL * 1024ULL};
constexpr auto kWebpMinQuality = 20;
constexpr auto kWebpQualityStep = 10;
constexpr auto kWebpFineQualityStep = 5;
constexpr auto kWebpSmallGapThreshold = std::uintmax_t{3ULL * 1024ULL * 1024ULL};
constexpr auto kSegmentDurationUs = std::uint64_t{10'000'000};

struct WebpEncodeContext {
  fs::path inputVidPath;
  fs::path outputFilePath;
  fs::path progressFilePath;
  std::function<void(std::string const&)> statusUpdater;
  // Keeps the forensic snapshot's subprocessCmdline in sync with the quality
  // tier actually being attempted.
  std::function<void(std::string const&)> cmdlineUpdater;
};

struct WebpEncodeStep {
  int exitCode;
  std::optional<std::uintmax_t> outputSize;
  std::optional<int> pid;
};

struct EncodeExecutionPlan {
  fs::path progressFilePath;
  std::optional<fs::path> outputPath;
  fs::path outputFilePath;
};

auto truncateEncodingStatus(std::string const& text, std::size_t maxLen = 256)
  -> std::string {
  auto sanitized = text;
  sanitized.erase(std::remove(sanitized.begin(), sanitized.end(), '\r'), sanitized.end());
  return displaytext::truncateWithEllipsis(sanitized, maxLen);
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
  workdirs::ensureScratchDir();
  auto progressFilePath = fs::path{};
  auto outputPath = std::optional<fs::path>{};
  auto plannedOutputFile = std::optional<fs::path>{};
  {
    auto lock = std::scoped_lock{state.mtx};
    if (!state.progressFilePath.has_value()) {
      state.progressFilePath =
        workdirs::scratchDir() / std::format("progress_{}.txt", getUUID());
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
  appctx::AppContext& ctx,
  appctx::EncodingState const& state,
  EncodeExecutionPlan const& plan
) -> EncodeConfig {
  auto const settings = resolveInputEncodeSettings(
    ctx.toolchain,
    ctx.runtime,
    state.inputPath,
    ctx.config.nvencPreset
  );

  return EncodeConfig{
    .ffmpegPath = ctx.toolchain.ffmpegPath,
    .inputPath = state.inputPath,
    .outputPath = plan.outputPath,
    .outputFilePath = plan.outputFilePath,
    .outputFormat = ctx.config.outputFormat,
    .videoCodec = ctx.config.videoCodec,
    .crf = state.chosenCq.has_value() ? state.chosenCq : ctx.config.crf,
    .nvencPreset = settings.nvencPreset,
    .maxrateKbps = settings.maxrateKbps,
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

  if (encodeCtx.cmdlineUpdater) { encodeCtx.cmdlineUpdater(cfg.buildCMD()); }

  if (auto const res = cfg.validate(); !res) {
    LOG_ERROR("{}", res.error());
    return {-1, std::nullopt, std::nullopt};
  }

  auto const [exitCode, _, pid] = exec2(cfg.buildCMD(), [&](std::string_view line) {
    reportEncodingDiagnostic(encodeCtx.statusUpdater, line);
  });
  if (exitCode != 0) {
    LOG_WARN(
      "WebP encoding step failed: input={} quality={} exitCode={}",
      encodeCtx.inputVidPath.string(),
      quality,
      exitCode
    );
    return {exitCode, std::nullopt, pid};
  }
  if (!fs::exists(outputFile)) { return {exitCode, std::nullopt, pid}; }

  LOG_DEBUG(
    "WebP encoding step output size: input={} quality={} bytes={}",
    encodeCtx.inputVidPath.string(),
    quality,
    fs::file_size(outputFile)
  );

  return {exitCode, fs::file_size(outputFile), pid};
}

enum class WebpAttemptResult {
  Succeeded,    // encoded under the target size
  Aborted,      // stop requested or the step failed permanently
  UnderTarget,  // still over target; try a lower quality
};

// One adaptive attempt: run the step, classify the outcome, and advance the
// quality when the result is still over target.
auto runWebpAdaptiveAttempt(
  appctx::AppContext const& appCtx,
  WebpEncodeContext const& encodeCtx,
  fs::path const& outputFile,
  unsigned& quality
) -> WebpAttemptResult {
  if (stopsignal::isStopRequested()) { return WebpAttemptResult::Aborted; }

  if (encodeCtx.statusUpdater) { encodeCtx.statusUpdater(std::format("q={}", quality)); }
  auto const stepRes = runWebpEncodingStep(appCtx, encodeCtx, quality, outputFile);
  if (
    stopsignal::isStopRequested() || stepRes.exitCode == stopsignal::kCanceledExitCode
  ) {
    return WebpAttemptResult::Aborted;
  }
  if (stepRes.exitCode != 0) {
    LOG_ERROR(
      "WebP encoding step failed permanently: input={} quality={} exitCode={}",
      encodeCtx.inputVidPath.string(),
      quality,
      stepRes.exitCode
    );
    return WebpAttemptResult::Aborted;
  }
  if (!stepRes.outputSize.has_value()) {
    LOG_ERROR(
      "WebP encoding step produced no output file: input={} quality={} output={}",
      encodeCtx.inputVidPath.string(),
      quality,
      outputFile.string()
    );
    return WebpAttemptResult::Aborted;
  }

  auto const outputSize = stepRes.outputSize.value();
  if (outputSize < kWebpTargetMaxSize) {
    LOG_DEBUG(
      "WebP encoded under target size: {} ({} bytes, q={})",
      outputFile.string(),
      outputSize,
      quality
    );
    return WebpAttemptResult::Succeeded;
  }

  auto const sizeGap = outputSize - kWebpTargetMaxSize;
  auto const step =
    sizeGap <= kWebpSmallGapThreshold ? kWebpFineQualityStep : kWebpQualityStep;
  auto const nextQuality = quality - step;
  if (encodeCtx.statusUpdater && nextQuality >= kWebpMinQuality) {
    auto const outputSizeMB = static_cast<double>(outputSize) / 1024.0 / 1024.0;
    encodeCtx
      .statusUpdater(std::format("retry q={} ({:.1f}MB)", nextQuality, outputSizeMB));
  }
  quality = nextQuality;
  return WebpAttemptResult::UnderTarget;
}

// Clears stale artifacts and logs the cancellation reason.
auto abortWebpForStopRequest(
  WebpEncodeContext const& encodeCtx,
  fs::path const& outputFile
) -> bool {
  clearWebpStaleFiles(encodeCtx.progressFilePath, outputFile);
  LOG_INFO(
    "WebP adaptive encoding canceled: input={} output={}",
    encodeCtx.inputVidPath.string(),
    outputFile.string()
  );
  return false;
}

// Minimum quality reached but still over target: keep the file with a warning.
auto webpMinQualityFallback(
  WebpEncodeContext const& encodeCtx,
  fs::path const& outputFile
) -> bool {
  if (!fs::exists(outputFile)) { return false; }
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

auto encodeWebpWithTargetSize(
  appctx::AppContext const& appCtx,
  WebpEncodeContext const& encodeCtx
) -> bool {
  auto const outputFile = encodeCtx.outputFilePath;

  LOG_DEBUG(
    "WebP adaptive encoding start: input={} output={} target={} bytes",
    encodeCtx.inputVidPath.string(),
    outputFile.string(),
    kWebpTargetMaxSize
  );

  auto const inputPathStr = encodeCtx.inputVidPath.string();
  logging::ScopedErrorContext ctx("video.encode.webp", inputPathStr);

  auto quality = 80u;
  while (quality >= kWebpMinQuality) {
    auto const attemptDetail =
      std::format("q={} target={} bytes", quality, kWebpTargetMaxSize);
    logging::ScopedErrorContext attemptCtx("webp.attempt", attemptDetail);

    auto const result = runWebpAdaptiveAttempt(appCtx, encodeCtx, outputFile, quality);
    if (result == WebpAttemptResult::Succeeded) {
      // The progress file is only needed while ffmpeg runs; drop it so the
      // final successful attempt leaves nothing behind (the per-attempt
      // clearWebpStaleFiles only cleans at the *start* of the next attempt).
      auto ec = std::error_code{};
      fs::remove(encodeCtx.progressFilePath, ec);
      return true;
    }
    if (result == WebpAttemptResult::Aborted) {
      if (stopsignal::isStopRequested()) {
        return abortWebpForStopRequest(encodeCtx, outputFile);
      }
      clearWebpStaleFiles(encodeCtx.progressFilePath, outputFile);
      return false;
    }
  }

  if (webpMinQualityFallback(encodeCtx, outputFile)) {
    // Fallback keeps the output; only the progress file is dropped.
    auto ec = std::error_code{};
    fs::remove(encodeCtx.progressFilePath, ec);
    return true;
  }

  clearWebpStaleFiles(encodeCtx.progressFilePath, outputFile);
  LOG_ERROR(
    "WebP adaptive encoding failed: input={} output={}",
    encodeCtx.inputVidPath.string(),
    outputFile.string()
  );

  return false;
}

auto segmentFilePath(fs::path const& segmentDir, std::uint64_t index) -> fs::path {
  return segmentDir / std::format("seg_{}.ts", index);
}

auto segmentProgressFilePath(fs::path const& segmentDir, std::uint64_t index)
  -> fs::path {
  return segmentDir / std::format("seg_{}.progress", index);
}

auto encodeOneSegment(
  appctx::AppContext& ctx,
  appctx::EncodingState& state,
  function_ref statusUpdater,
  fs::path const& segmentDir,
  std::uint64_t index,
  std::uint64_t startUs,
  std::uint64_t durationUs,
  std::size_t workerCount
) -> bool {
  auto const segFile = segmentFilePath(segmentDir, index);
  auto const segProgressFile = segmentProgressFilePath(segmentDir, index);
  {
    auto ec = std::error_code{};
    fs::remove(segProgressFile, ec);
  }

  auto const settings = resolveInputEncodeSettings(
    ctx.toolchain,
    ctx.runtime,
    state.inputPath,
    ctx.config.nvencPreset
  );

  auto const cfg = buildSegmentEncodeConfig(
    ctx.toolchain,
    state.inputPath,
    ctx.config.outputFormat,
    state.chosenCq.has_value() ? state.chosenCq : ctx.config.crf,
    ctx.config.videoCodec,
    settings,
    index,
    startUs,
    durationUs,
    segFile,
    segProgressFile,
    workerCount
  );

  if (auto const validationResult = cfg.validate(); !validationResult) {
    LOG_ERROR(
      "Segment config invalid: input={} segment={} error={}",
      state.inputPath.string(),
      index,
      validationResult.error()
    );
    return false;
  }

  {
    auto lock = std::scoped_lock{state.mtx};
    state.progressFilePath = segProgressFile;
    state.subprocessCmdline = cfg.buildCMD();
  }

  auto const [exitCode, _, pid] = exec2(cfg.buildCMD(), [&](std::string_view line) {
    reportEncodingDiagnostic(statusUpdater, line);
  });
  if (pid.has_value()) {
    auto lock = std::scoped_lock{state.mtx};
    state.subprocessPid = pid;
  }
  if (exitCode != 0) {
    LOG_WARN(
      "Segment encode exited with non-zero code: input={} segment={} exitCode={}",
      state.inputPath.string(),
      index,
      exitCode
    );
    return false;
  }

  return fs::exists(segFile);
}

auto ensureAudioFile(
  appctx::AppContext& ctx,
  appctx::EncodingState const& state,
  fs::path const& segmentDir,
  function_ref statusUpdater
) -> eh::Result<std::optional<fs::path>> {
  auto fallbackAudio = segmentDir / "audio.m4a";
  if (fs::exists(fallbackAudio)) { return fallbackAudio; }

  auto copyAudio =
    segmentDir / std::format("audio{}", state.inputPath.extension().string());
  if (fs::exists(copyAudio)) { return copyAudio; }

  auto const hasAudioRes = getVidHasAudio(ctx.toolchain, ctx.runtime, state.inputPath);
  if (!hasAudioRes) {
    return eh::makeError("Failed to probe audio stream: {}", hasAudioRes.error());
  }
  if (!hasAudioRes.value()) { return std::nullopt; }

  auto const runExtraction = [&](bool aacFallback, fs::path const& audioPath) {
    auto const cmd = buildAudioExtractionCmd(
      ctx.toolchain.ffmpegPath.value_or(fs::path{"ffmpeg"}),
      state.inputPath,
      audioPath,
      aacFallback
    );
    auto const [exitCode, _, pid] = exec2(cmd, [&](std::string_view line) {
      reportEncodingDiagnostic(statusUpdater, line);
    });
    if (pid.has_value()) {
      LOG_DEBUG(  // NOLINT(bugprone-lambda-function-name): SPDLOG_FUNCTION in exec2 callback lambda
        "Audio extraction pid: {}",
        pid.value()
      );
    }
    return exitCode == 0 && fs::exists(audioPath);
  };

  if (runExtraction(false, copyAudio)) { return copyAudio; }

  auto ec = std::error_code{};
  fs::remove(copyAudio, ec);

  if (runExtraction(true, fallbackAudio)) { return fallbackAudio; }

  return eh::makeError("Failed to extract audio from: {}", state.inputPath.string());
}

auto assembleSegments(
  appctx::AppContext const& ctx,
  appctx::EncodingState& state,
  EncodeExecutionPlan const& plan,
  fs::path const& segmentDir,
  std::uint64_t segmentCount,
  std::optional<fs::path> const& audioPath,
  function_ref statusUpdater
) -> bool {
  auto const listPath = segmentDir / "list.txt";
  {
    auto out = std::ofstream{listPath};
    if (!out) {
      LOG_ERROR("Failed to write segment list: {}", listPath.string());
      return false;
    }
    for (auto index = std::uint64_t{0}; index < segmentCount; ++index) {
      auto pathStr = segmentFilePath(segmentDir, index).string();
      std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
      out << "file '" << pathStr << "'\n";
    }
  }

  auto const cmd = buildSegmentAssemblyCmd(
    ctx.toolchain.ffmpegPath.value_or(fs::path{"ffmpeg"}),
    listPath,
    audioPath,
    plan.outputFilePath
  );

  {
    auto lock = std::scoped_lock{state.mtx};
    state.subprocessCmdline = cmd;
  }

  auto const [exitCode, _, pid] = exec2(cmd, [&](std::string_view line) {
    reportEncodingDiagnostic(statusUpdater, line);
  });
  if (pid.has_value()) {
    auto lock = std::scoped_lock{state.mtx};
    state.subprocessPid = pid;
  }
  if (exitCode != 0) {
    LOG_WARN(
      "Segment assembly exited with non-zero code: input={} exitCode={}",
      state.inputPath.string(),
      exitCode
    );
    return false;
  }
  if (!fs::exists(plan.outputFilePath)) {
    LOG_WARN(
      "Segment assembly produced no output: input={} output={}",
      state.inputPath.string(),
      plan.outputFilePath.string()
    );
    return false;
  }

  return true;
}

// NOLINTNEXTLINE(readability-function-size): linear segment loop; phase comments delimit blocks
auto runSegmentedEncoding(
  appctx::AppContext& ctx,
  appctx::EncodingState& state,
  EncodeExecutionPlan const& plan,
  function_ref statusUpdater,
  std::size_t workerCount
) -> bool {
  auto const taskId = state.actionId.value_or(
    std::format("encode:{}", collisionnaming::stablePathString(state.inputPath))
  );
  auto const workRootRes = workdirs::resolveWorkRoot(ctx.config);
  if (!workRootRes) { return failEncoding(state, workRootRes.error()); }
  auto const segmentDir = videoseg::segmentDirForTask(*workRootRes, taskId);

  auto resumeSegment = std::uint64_t{0};
  auto resumeTimeUs = std::uint64_t{0};
  auto const store = ctx.runtime.jobState;
  auto const task = store ? store->findTask(taskId) : std::nullopt;
  if (task.has_value() && task->segmentIndex.has_value()) {
    resumeSegment = task->segmentIndex.value();
    resumeTimeUs = task->resumeTimeUs.value_or(0);
  } else {
    videoseg::removeSegmentDir(segmentDir);
  }
  videoseg::createSegmentDir(segmentDir);

  for (auto index = std::uint64_t{0}; index < resumeSegment; ++index) {
    if (!fs::exists(segmentFilePath(segmentDir, index))) {
      resumeSegment = index;
      resumeTimeUs = index * kSegmentDurationUs;
      break;
    }
  }

  auto const durationRes =
    getVidTotalDurationUs(ctx.toolchain, ctx.runtime, state.inputPath);
  if (!durationRes) { return failEncoding(state, durationRes.error()); }
  auto const totalDurationUs = durationRes.value();
  if (totalDurationUs == 0) {
    return failEncoding(
      state,
      std::format("Cannot segment video with zero duration: {}", state.inputPath.string())
    );
  }
  auto const segmentCount =
    (totalDurationUs + kSegmentDurationUs - 1) / kSegmentDurationUs;

  auto const audioRes = ensureAudioFile(ctx, state, segmentDir, statusUpdater);
  if (!audioRes) { return failEncoding(state, audioRes.error()); }
  auto const& audioPath = audioRes.value();

  auto totalFrames = std::int64_t{0};
  if (
    auto const framesRes = getVidTotalFrames(ctx.toolchain, ctx.runtime, state.inputPath);
    framesRes.has_value()
  ) {
    totalFrames = framesRes.value();
  }

  auto const setBaseFrameOffset = [&](std::uint64_t cumulativeUs) {
    auto const offset =
      segmentBaseFrameOffset(cumulativeUs, totalFrames, totalDurationUs);
    auto lock = std::scoped_lock{state.mtx};
    state.baseFrameOffset = offset;
  };
  setBaseFrameOffset(resumeTimeUs);

  for (auto index = resumeSegment; index < segmentCount; ++index) {
    if (stopsignal::isStopRequested()) { return false; }

    auto const startUs = index * kSegmentDurationUs;
    auto const durationUs = std::min(kSegmentDurationUs, totalDurationUs - startUs);

    if (statusUpdater) {
      statusUpdater(std::format("segment {}/{}", index + 1, segmentCount));
    }

    LOG_DEBUG(
      "Encoding segment: input={} segment={}/{} start={}us duration={}us",
      state.inputPath.string(),
      index + 1,
      segmentCount,
      startUs,
      durationUs
    );

    if (!encodeOneSegment(
          ctx,
          state,
          statusUpdater,
          segmentDir,
          index,
          startUs,
          durationUs,
          workerCount
        )) {
      return false;
    }

    auto const parsedEndUs =
      parseSegmentEndUs(segmentProgressFilePath(segmentDir, index));
    if (!parsedEndUs.has_value()) {
      LOG_WARN(
        "Failed to parse segment end time; falling back to nominal duration {}us "
        "(segment {} of {})",
        durationUs,
        index + 1,
        segmentCount
      );
    }
    auto const actualUs = parsedEndUs.value_or(durationUs);
    resumeTimeUs += actualUs;
    setBaseFrameOffset(resumeTimeUs);
    if (store) { store->markSegmentProgress(taskId, index + 1, resumeTimeUs); }
  }

  if (!assembleSegments(
        ctx,
        state,
        plan,
        segmentDir,
        segmentCount,
        audioPath,
        statusUpdater
      )) {
    return false;
  }

  videoseg::removeSegmentDir(segmentDir);
  return true;
}

}  // namespace

bool encodeVideo(
  appctx::AppContext& ctx,
  appctx::EncodingState& state,
  function_ref statusUpdater,
  std::size_t workerCount
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

  {
    auto lock = std::scoped_lock{state.mtx};
    state.subprocessCmdline = cfg.buildCMD();
  }

  auto const inputPathStr = state.inputPath.string();
  logging::ScopedErrorContext scopedCtx("video.encode", inputPathStr);

  LOG_DEBUG("Encoding video: {}", state.inputPath.string());

  if (ctx.config.outputFormat == "webp") {
    return encodeWebpWithTargetSize(
      ctx,
      WebpEncodeContext{
        .inputVidPath = state.inputPath,
        .outputFilePath = executionPlan.outputFilePath,
        .progressFilePath = executionPlan.progressFilePath,
        .statusUpdater = statusUpdater,
        .cmdlineUpdater = [&state](std::string const& cmd) {
          auto lock = std::scoped_lock{state.mtx};
          state.subprocessCmdline = cmd;
        },
      }
    );
  }

  return runSegmentedEncoding(ctx, state, executionPlan, statusUpdater, workerCount);
}
