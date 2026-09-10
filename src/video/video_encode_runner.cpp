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
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::VIDEO_ENCODE);

namespace fs = std::filesystem;

namespace {

constexpr auto kWebpTargetMaxSize = std::uintmax_t{20ULL * 1024ULL * 1024ULL};
constexpr auto kWebpMinQuality = 20;
constexpr auto kWebpQualityStep = 10;
constexpr auto kWebpFineQualityStep = 5;
constexpr auto kWebpSmallGapThreshold = std::uintmax_t{3ULL * 1024ULL * 1024ULL};
constexpr auto kSegmentListPollInterval = std::chrono::milliseconds{100};

struct WebpEncodeContext {
  fs::path inputVidPath;
  fs::path outputFilePath;
  fs::path progressFilePath;
  std::function<void(std::string const&)> statusUpdater;
  // Keeps the forensic snapshot's subprocessCmdline in sync with the quality
  // tier actually being attempted.
  std::function<void(std::string const&)> cmdlineUpdater;
  // Receives the child's diagnostic line so the failed-file list can name it.
  appctx::EncodingState& state;
};

struct WebpEncodeStep {
  int exitCode;
  std::optional<std::uintmax_t> outputSize;
};

struct EncodeExecutionPlan {
  fs::path progressFilePath;
  fs::path outputFilePath;
};

// The output side of segmented encoding: the segments to assemble, in the
// order the encoder's list recorded them, and the transcript audio, if any.
struct SegmentAssemblySpec {
  fs::path segmentDir;
  std::vector<std::string> segmentNames;
  std::optional<fs::path> audioPath;
};

auto truncateEncodingStatus(std::string const& text, std::size_t maxLen = 256)
  -> std::string {
  auto sanitized = text;
  sanitized.erase(std::remove(sanitized.begin(), sanitized.end(), '\r'), sanitized.end());
  return displaytext::truncateWithEllipsis(sanitized, maxLen);
}

bool failEncoding(appctx::EncodingState& state, std::string const& error) {
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
  auto plannedOutputFile = std::optional<fs::path>{};
  {
    auto lock = std::scoped_lock{state.mtx};
    if (!state.progressFilePath.has_value()) {
      state.progressFilePath =
        workdirs::scratchDir() / std::format("progress_{}.txt", getUUID());
    }
    progressFilePath = state.progressFilePath.value();
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
    .outputFilePath = plannedOutputFile.value(),
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
    return {-1, std::nullopt};
  }

  auto const [exitCode, capturedOutput, pid, stderrText] =
    exec2(cfg.buildCMD(), [&](std::string_view line) {
      reportEncodingDiagnostic(encodeCtx.statusUpdater, line);
    });
  if (exitCode != 0) {
    auto const reason =
      extractFailureReason(capturedOutput, stderrText, exitCode, isLikelyFfmpegErrorLine);
    LOG_WARN(
      "WebP encoding step failed: input={} quality={} exitCode={} reason={}",
      encodeCtx.inputVidPath.string(),
      quality,
      exitCode,
      reason
    );
    {
      auto lock = std::scoped_lock{encodeCtx.state.mtx};
      encodeCtx.state.lastError = reason;
    }
    return {exitCode, std::nullopt};
  }
  if (!fs::exists(outputFile)) {
    auto const reason =
      std::format("encoder produced no output file: {}", outputFile.string());
    {
      auto lock = std::scoped_lock{encodeCtx.state.mtx};
      encodeCtx.state.lastError = reason;
    }
    return {exitCode, std::nullopt};
  }

  LOG_DEBUG(
    "WebP encoding step output size: input={} quality={} bytes={}",
    encodeCtx.inputVidPath.string(),
    quality,
    fs::file_size(outputFile)
  );

  return {exitCode, fs::file_size(outputFile)};
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
bool abortWebpForStopRequest(
  WebpEncodeContext const& encodeCtx,
  fs::path const& outputFile
) {
  clearWebpStaleFiles(encodeCtx.progressFilePath, outputFile);
  LOG_INFO(
    "WebP adaptive encoding canceled: input={} output={}",
    encodeCtx.inputVidPath.string(),
    outputFile.string()
  );
  return false;
}

// Minimum quality reached but still over target: keep the file with a warning.
bool webpMinQualityFallback(
  WebpEncodeContext const& encodeCtx,
  fs::path const& outputFile
) {
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

bool encodeWebpWithTargetSize(
  appctx::AppContext const& appCtx,
  WebpEncodeContext const& encodeCtx
) {
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

// Completion signal of the single-pass encode: every row the muxer has closed
// is a segment that is complete on disk, while the row still being written
// never counts. Recording happens here instead of after a per-segment process.
struct SegmentListWatch {
  fs::path listPath;
  std::string taskId;
  std::uint64_t startNumber = 0;
  std::uint64_t segmentTotal = 0;
  jobstate::Store* store = nullptr;
  std::function<void(std::string const&)> statusUpdater;
  std::atomic<std::uint64_t> completedSegments{0};
};

void markCompletedSegments(SegmentListWatch& watch) {
  auto const entries = parseSegmentList(watch.listPath);
  auto const total = watch.startNumber + static_cast<std::uint64_t>(entries.size());
  if (total <= watch.completedSegments.load(std::memory_order_acquire)) { return; }

  watch.completedSegments.store(total, std::memory_order_release);
  if (watch.store != nullptr) {
    watch.store->markSegmentProgress(watch.taskId, total, total * kSegmentDurationUs);
  }
  if (watch.statusUpdater) {
    watch.statusUpdater(std::format("segment {}/{}", total, watch.segmentTotal));
  }
}

// Segments an earlier attempt already finished, in order. The recorded count is
// the authority: the muxer rewrites its list for every attempt, so the list only
// ever describes the last one, while job state accumulates the total. Names are
// derived from the index because the muxer updates list rows in place and may
// pad a name with spaces, while the files follow the naming pattern.
auto reusableSegments(fs::path const& segmentDir, std::uint64_t storedSegments)
  -> std::vector<std::string> {
  auto reusable = std::vector<std::string>{};
  for (auto index = std::uint64_t{0}; index < storedSegments; ++index) {
    auto const name = segmentFileName(index);
    if (!fs::exists(segmentDir / name)) { break; }
    reusable.push_back(name);
  }
  return reusable;
}

// True when the encoder has nothing left to do: every segment mark is already on
// disk, or the muxer's cut cadence produced fewer segments than the duration
// implies and its list reaches the end of the timeline. Listed times carry the
// encoder's reorder delay, so a complete list ends at or just past the duration
// while a run that died with a segment in flight stops a whole segment short.
auto encodeComplete(
  std::uint64_t completed,
  std::uint64_t segmentTotal,
  std::span<SegmentListEntry const> listedSegments,
  std::uint64_t totalDurationUs
) -> bool {
  if (completed >= segmentTotal) { return true; }
  return !listedSegments.empty()
    && completed >= listedSegments.size()
    && listedSegments.back().endUs >= totalDurationUs;
}

void watchSegmentList(SegmentListWatch& watch, std::stop_token const& stopToken) {
  while (!stopToken.stop_requested()) {
    markCompletedSegments(watch);
    std::this_thread::sleep_for(kSegmentListPollInterval);
  }
  markCompletedSegments(watch);
}

// Runs the whole segment series in one ffmpeg invocation and blocks until it
// exits, recording segments as the encoder closes them.
bool runEncoderSeries(
  appctx::EncodingState& state,
  SegmentListWatch& watch,
  EncodeConfig const& cfg
) {
  auto const cmd = cfg.buildCMD();
  {
    auto lock = std::scoped_lock{state.mtx};
    state.subprocessCmdline = cmd;
  }

  // NOLINTNEXTLINE(performance-unnecessary-value-param): std::jthread invokes the callable with a stop_token value
  auto watcher = std::jthread{[&watch](std::stop_token stopToken) {
    watchSegmentList(watch, stopToken);
  }};
  auto const [exitCode, capturedOutput, pid, stderrText] =
    exec2(cmd, [&](std::string_view line) {
      reportEncodingDiagnostic(watch.statusUpdater, line);
    });
  watcher.request_stop();
  watcher.join();

  if (pid.has_value()) {
    auto lock = std::scoped_lock{state.mtx};
    state.subprocessPid = pid;
  }
  if (exitCode == 0) { return true; }

  auto const reason =
    extractFailureReason(capturedOutput, stderrText, exitCode, isLikelyFfmpegErrorLine);
  LOG_WARN(
    "Segment series encode exited with non-zero code: input={} completed={} exitCode={} "
    "reason={}",
    state.inputPath.string(),
    watch.completedSegments.load(),
    exitCode,
    reason
  );
  {
    auto lock = std::scoped_lock{state.mtx};
    state.lastError = reason;
  }
  return false;
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
    auto const [exitCode, _, pid, stderrText] = exec2(cmd, [&](std::string_view line) {
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

bool assembleSegments(
  appctx::AppContext const& ctx,
  appctx::EncodingState& state,
  EncodeExecutionPlan const& plan,
  SegmentAssemblySpec const& spec,
  function_ref statusUpdater
) {
  auto const listPath = spec.segmentDir / "list.txt";
  if (!writeConcatManifest(listPath, spec.segmentNames)) { return false; }

  auto const cmd = buildSegmentAssemblyCmd(
    ctx.toolchain.ffmpegPath.value_or(fs::path{"ffmpeg"}),
    listPath,
    spec.audioPath,
    plan.outputFilePath
  );

  {
    auto lock = std::scoped_lock{state.mtx};
    state.subprocessCmdline = cmd;
  }

  auto const [exitCode, capturedOutput, pid, stderrText] =
    exec2(cmd, [&](std::string_view line) {
      reportEncodingDiagnostic(statusUpdater, line);
    });
  if (pid.has_value()) {
    auto lock = std::scoped_lock{state.mtx};
    state.subprocessPid = pid;
  }
  if (exitCode != 0) {
    auto const reason =
      extractFailureReason(capturedOutput, stderrText, exitCode, isLikelyFfmpegErrorLine);
    LOG_WARN(
      "Segment assembly exited with non-zero code: input={} exitCode={} reason={}",
      state.inputPath.string(),
      exitCode,
      reason
    );
    {
      auto lock = std::scoped_lock{state.mtx};
      state.lastError = reason;
    }
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

// NOLINTNEXTLINE(readability-function-size): encode and assemble phases; comments delimit blocks
bool runSegmentedEncoding(
  appctx::AppContext& ctx,
  appctx::EncodingState& state,
  EncodeExecutionPlan const& plan,
  function_ref statusUpdater,
  std::size_t workerCount
) {
  auto const taskId = state.actionId.value_or(
    std::format("encode:{}", collisionnaming::stablePathString(state.inputPath))
  );
  auto const workRootRes = workdirs::resolveWorkRoot(ctx.config);
  if (!workRootRes) { return failEncoding(state, workRootRes.error()); }
  auto const segmentDir = videoseg::segmentDirForTask(*workRootRes, taskId);
  auto const listPath = segmentListPath(segmentDir);

  auto const store = ctx.runtime.jobState;
  auto const task = store ? store->findTask(taskId) : std::nullopt;
  auto const storedSegments =
    task.has_value() ? task->segmentIndex.value_or(0) : std::uint64_t{0};
  if (storedSegments == 0) {
    // No recorded progress: start clean. A --restart run lands here too.
    videoseg::removeSegmentDir(segmentDir);
  }
  videoseg::createSegmentDir(segmentDir);

  // Which earlier segments are reusable: see reusableSegments for the naming
  // and verification rules.
  auto const previousEntries = parseSegmentList(listPath);
  auto completedNames = reusableSegments(segmentDir, storedSegments);

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
  auto const segmentTotal =
    (totalDurationUs + kSegmentDurationUs - 1) / kSegmentDurationUs;
  auto const completed = static_cast<std::uint64_t>(completedNames.size());
  auto const resumeUs = completed * kSegmentDurationUs;

  auto const audioRes = ensureAudioFile(ctx, state, segmentDir, statusUpdater);
  if (!audioRes) { return failEncoding(state, audioRes.error()); }
  auto const& audioPath = audioRes.value();

  auto const assemble = [&](std::vector<std::string> names) {
    auto const assembly = SegmentAssemblySpec{
      .segmentDir = segmentDir,
      .segmentNames = std::move(names),
      .audioPath = audioPath,
    };
    if (!assembleSegments(ctx, state, plan, assembly, statusUpdater)) { return false; }
    videoseg::removeSegmentDir(segmentDir);
    return true;
  };

  // Nothing left to encode: every segment is on disk already (a run interrupted
  // during assembly), or the muxer's cut cadence produced fewer segments than
  // the duration implies and its list covers the whole timeline.
  if (encodeComplete(completed, segmentTotal, previousEntries, totalDurationUs)) {
    LOG_DEBUG(
      "All {} segment(s) already encoded; assembling only: input={}",
      completed,
      state.inputPath.string()
    );
    return assemble(std::move(completedNames));
  }

  auto totalFrames = std::int64_t{0};
  if (
    auto const framesRes = getVidTotalFrames(ctx.toolchain, ctx.runtime, state.inputPath);
    framesRes.has_value()
  ) {
    totalFrames = framesRes.value();
  }
  {
    // One continuous progress counter spans the whole run, so only the resumed
    // prefix needs an offset.
    auto lock = std::scoped_lock{state.mtx};
    state.baseFrameOffset =
      segmentBaseFrameOffset(resumeUs, totalFrames, totalDurationUs);
  }

  auto const settings = resolveInputEncodeSettings(
    ctx.toolchain,
    ctx.runtime,
    state.inputPath,
    ctx.config.nvencPreset
  );
  auto const cfg = buildSegmentSeriesConfig(
    ctx.toolchain,
    state.inputPath,
    SegmentSeries{
      .segmentDir = segmentDir,
      .startNumber = completed,
      .resumeUs = resumeUs,
    },
    EncodeProfile{
      .outputFormat = ctx.config.outputFormat,
      .videoCodec = ctx.config.videoCodec,
      .crf = state.chosenCq.has_value() ? state.chosenCq : ctx.config.crf,
      .settings = settings,
      .workerCount = workerCount,
    },
    plan.progressFilePath
  );
  if (auto const validationResult = cfg.validate(); !validationResult) {
    return failEncoding(
      state,
      std::format(
        "Segment series config invalid: input={} error={}",
        state.inputPath.string(),
        validationResult.error()
      )
    );
  }

  LOG_DEBUG(
    "Encoding segment series: input={} resume={}us completed={}/{} startNumber={}",
    state.inputPath.string(),
    resumeUs,
    completed,
    segmentTotal,
    completed
  );
  if (statusUpdater) {
    statusUpdater(std::format("segment {}/{}", completed + 1, segmentTotal));
  }

  // ffmpeg rewrites the list, so the poller must not read the previous
  // attempt's rows; those are already captured in completedNames.
  auto ec = std::error_code{};
  fs::remove(listPath, ec);

  auto watch = SegmentListWatch{
    .listPath = listPath,
    .taskId = taskId,
    .startNumber = completed,
    .segmentTotal = segmentTotal,
    .store = store.get(),
    .statusUpdater = statusUpdater,
    .completedSegments = completed,
  };
  if (!runEncoderSeries(state, watch, cfg)) { return false; }
  if (stopsignal::isStopRequested()) { return false; }

  // This run's segments continue the numbering after the resumed prefix.
  auto names = std::move(completedNames);
  auto const runSegments = parseSegmentList(listPath).size();
  for (auto index = std::size_t{0}; index < runSegments; ++index) {
    names.push_back(segmentFileName(completed + static_cast<std::uint64_t>(index)));
  }
  if (names.size() <= completed) {
    return failEncoding(
      state,
      std::format("Segment encode produced no segments: {}", state.inputPath.string())
    );
  }

  return assemble(std::move(names));
}

}  // namespace

// Concat-demuxer rule: manifest entries resolve from the manifest's own
// directory, so bare segment names work for relative and absolute runs alike.
bool writeConcatManifest(
  fs::path const& listPath,
  std::span<std::string const> segmentNames
) {
  auto out = std::ofstream{listPath};
  if (!out) {
    LOG_ERROR("Failed to write segment list: {}", listPath.string());
    return false;
  }
  for (auto const& name: segmentNames) { out << "file '" << name << "'" << "\n"; }
  return true;
}

bool encodeVideo(
  appctx::AppContext& ctx,
  appctx::EncodingState& state,
  function_ref statusUpdater,
  std::size_t workerCount
) {
  auto const executionPlanRes = prepareEncodeExecution(state);
  if (!executionPlanRes) { return failEncoding(state, executionPlanRes.error()); }

  auto const& executionPlan = executionPlanRes.value();
  auto const inputPathStr = state.inputPath.string();
  logging::ScopedErrorContext scopedCtx("video.encode", inputPathStr);

  LOG_DEBUG(
    "Encoding video: input={} output-format={} output-file={} progress-file={}",
    state.inputPath.string(),
    ctx.config.outputFormat,
    executionPlan.outputFilePath.string(),
    executionPlan.progressFilePath.string()
  );

  if (ctx.config.outputFormat == "webp") {
    return encodeWebpWithTargetSize(
      ctx,
      WebpEncodeContext{
        .inputVidPath = state.inputPath,
        .outputFilePath = executionPlan.outputFilePath,
        .progressFilePath = executionPlan.progressFilePath,
        .statusUpdater = statusUpdater,
        .cmdlineUpdater =
          [&state](std::string const& cmd) {
            auto lock = std::scoped_lock{state.mtx};
            state.subprocessCmdline = cmd;
          },
        .state = state,
      }
    );
  }

  return runSegmentedEncoding(ctx, state, executionPlan, statusUpdater, workerCount);
}
