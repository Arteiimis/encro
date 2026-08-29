#include "preview/preview_process.h"

#include "infra/open_file.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "core/progress.h"
#include "core/task_executor.h"
#include "core/work_dirs.h"
#include "utils/utils.h"
#include "video/encode_probe.h"
#include "video/video_info.h"
#include "video/video_quality.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::PREVIEW_PROCESS);

namespace fs = std::filesystem;
using enum terminal::MessageKind;

namespace preview {

namespace {

constexpr auto kWindowCount = std::size_t{5};
constexpr auto kWindowDurationUs = std::uint64_t{10'000'000};
constexpr auto kFullComparisonBudgetUs = std::uint64_t{50'000'000};

double seconds(std::uint64_t micros) {
  return static_cast<double>(micros) / 1'000'000.0;
}

auto parseDouble(std::string_view text) -> std::optional<double> {
  try {
    return std::stod(std::string{text});
  } catch (...) { return std::nullopt; }
}

auto parseFraction(std::string_view text) -> std::optional<double> {
  auto const slashPos = text.find('/');
  if (slashPos == std::string_view::npos) { return parseDouble(text); }
  auto const num = parseDouble(text.substr(0, slashPos));
  auto const den = parseDouble(text.substr(slashPos + 1));
  if (!num.has_value() || !den.has_value() || den.value() == 0.0) { return std::nullopt; }
  return num.value() / den.value();
}

namespace {

auto applyVideoStreamFields(
  boost::json::object const& stream,
  VideoProbe& probe,
  fs::path const& path
) -> eh::Result<void> {
  if (
    auto const codecIt = stream.find("codec_name");
    codecIt != stream.end() && codecIt->value().is_string()
  ) {
    auto const codecName = std::string_view{codecIt->value().as_string()};
    if (codecName == "webp" || codecName == "webp_anim") {
      return eh::makeError(
        "Preview supports video comparison only; input is WebP: {}",
        path.string()
      );
    }
  }
  if (
    auto const widthIt = stream.find("width");
    widthIt != stream.end() && widthIt->value().is_int64()
  ) {
    probe.width = static_cast<int>(widthIt->value().as_int64());
  }
  if (
    auto const heightIt = stream.find("height");
    heightIt != stream.end() && heightIt->value().is_int64()
  ) {
    probe.height = static_cast<int>(heightIt->value().as_int64());
  }
  if (
    auto const rateIt = stream.find("avg_frame_rate");
    rateIt != stream.end() && rateIt->value().is_string()
  ) {
    probe.fps =
      parseFraction(std::string_view{rateIt->value().as_string()}).value_or(0.0);
  }
  return {};
}

void applyAudioStreamFields(boost::json::object const& stream, VideoProbe& probe) {
  probe.hasAudio = true;
  if (
    auto const codecIt = stream.find("codec_name");
    codecIt != stream.end() && codecIt->value().is_string()
  ) {
    probe.audioCodec = std::string{codecIt->value().as_string()};
  }
}

auto probeStreamMetadata(
  boost::json::value const& streamsVal,
  VideoProbe& probe,
  fs::path const& path
) -> eh::Result<bool> {
  auto foundVideo = false;
  for (auto const& streamVal: streamsVal.as_array()) {
    if (!streamVal.is_object()) { continue; }
    auto const& stream = streamVal.as_object();
    auto const codecTypeIt = stream.find("codec_type");
    if (codecTypeIt == stream.end() || !codecTypeIt->value().is_string()) { continue; }
    auto const codecType = std::string_view{codecTypeIt->value().as_string()};

    if (codecType == "video") {
      foundVideo = true;
      if (auto const applied = applyVideoStreamFields(stream, probe, path); !applied) {
        return eh::makeError("{}", applied.error());
      }
    } else if (codecType == "audio") {
      applyAudioStreamFields(stream, probe);
    }
  }
  return foundVideo;
}

// Prefer the video stream's own duration: format.duration can be dragged
// past the video end by a longer audio track, and windows must stay within
// the video.
std::uint64_t probeVideoStreamDurationUs(boost::json::value const& info) {
  auto const streamsIt = info.as_object().find("streams");
  if (streamsIt == info.as_object().end() || !streamsIt->value().is_array()) { return 0; }
  for (auto const& stream: streamsIt->value().as_array()) {
    if (!stream.is_object()) { continue; }
    auto const codecIt = stream.as_object().find("codec_type");
    if (
      codecIt == stream.as_object().end()
      || !codecIt->value().is_string()
      || std::string_view{codecIt->value().as_string()} != "video"
    ) {
      continue;
    }
    if (
      auto const streamDurIt = stream.as_object().find("duration");
      streamDurIt != stream.as_object().end() && streamDurIt->value().is_string()
    ) {
      if (
        auto const duration =
          parseDouble(std::string_view{streamDurIt->value().as_string()})
      ) {
        return static_cast<std::uint64_t>(std::llround(duration.value() * 1'000'000.0));
      }
    }
    break;
  }
  return 0;
}

// Fall back to format.duration when the video stream carries none.
std::uint64_t probeFormatDurationUs(boost::json::value const& info) {
  auto const formatIt = info.as_object().find("format");
  if (formatIt == info.as_object().end() || !formatIt->value().is_object()) { return 0; }
  auto const durationIt = formatIt->value().as_object().find("duration");
  if (
    durationIt != formatIt->value().as_object().end() && durationIt->value().is_string()
  ) {
    if (
      auto const duration = parseDouble(std::string_view{durationIt->value().as_string()})
    ) {
      return static_cast<std::uint64_t>(std::llround(duration.value() * 1'000'000.0));
    }
  }
  return 0;
}

}  // namespace

// ── Implementation ──────────────────────────────────────────────────────────

auto probeVideo(
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  fs::path const& path
) -> eh::Result<VideoProbe> {
  auto const info = getVidInfo(toolchain, path);
  runtime.videoInfoCache.set(path, info);
  if (!info.is_object()) {
    return eh::makeError("Failed to probe video: {}", path.string());
  }

  auto const streamsIt = info.as_object().find("streams");
  if (streamsIt == info.as_object().end() || !streamsIt->value().is_array()) {
    return eh::makeError("Failed to probe video: {}", path.string());
  }

  auto probe = VideoProbe{};
  auto const foundVideo = probeStreamMetadata(streamsIt->value(), probe, path);
  if (!foundVideo) { return eh::makeError("{}", foundVideo.error()); }
  if (
    !foundVideo.value()
  ) {  // NOLINT(bugprone-unchecked-optional-access): guarded by the !foundVideo check above
    return eh::makeError("Not a video file: {}", path.string());
  }
  if (probe.width == 0 || probe.height == 0) {
    return eh::makeError("No video stream dimensions found for: {}", path.string());
  }

  probe.durationUs = probeVideoStreamDurationUs(info);
  if (probe.durationUs == 0) { probe.durationUs = probeFormatDurationUs(info); }

  return probe;
}

auto metricText(videoquality::QualityMetric metric) -> std::string_view {
  return videoquality::metricName(metric);
}

void printWindows(
  std::span<Window const> windows,
  std::optional<std::size_t> worstIndex
) {
  terminal::println(
    Info,
    "Preview windows ({}):",
    windows.size() == 1 ? "full comparison" : "5 x 10s"
  );
  for (auto index = std::size_t{}; index < windows.size(); ++index) {
    auto const& window = windows[index];
    auto scoreText = std::string{"-"};
    if (window.score.has_value()) {
      if (window.metric == videoquality::QualityMetric::Xpsnr) {
        scoreText =
          std::format("{} {:.2f} dB", metricText(window.metric), window.score.value());
      } else if (window.metric == videoquality::QualityMetric::Vmaf) {
        scoreText =
          std::format("{} {:.1f}", metricText(window.metric), window.score.value());
      } else {
        scoreText =
          std::format("{} {:.3f}", metricText(window.metric), window.score.value());
      }
    }
    auto suffix =
      worstIndex.has_value() && worstIndex.value() == index ? "  (worst)" : "";
    terminal::println(
      Info,
      "  [{}] {}  {}{}",
      index + 1,
      formatTimeRange(window.startUs, window.durationUs),
      scoreText,
      suffix
    );
  }
}

}  // namespace

auto pickPreviewWindows(
  std::uint64_t shorterDurationUs,
  std::optional<std::pair<double, double>> manualRange
) -> eh::Result<std::vector<Window>> {
  if (manualRange.has_value()) {
    auto const startSec = manualRange->first;
    auto const durationSec = manualRange->second;
    auto const totalSec = seconds(shorterDurationUs);
    if (startSec < 0.0 || std::isnan(startSec)) {
      return eh::makeError("--start must be a non-negative number.");
    }
    if (startSec >= totalSec) {
      return eh::makeError(
        "--start {}s is beyond the video duration ({:.1f}s).",
        startSec,
        totalSec
      );
    }
    auto const clamped = std::min(durationSec, totalSec - startSec);
    if (clamped <= 0.0) {
      return eh::makeError(
        "Invalid preview range: --start {}s --duration {}s.",
        startSec,
        durationSec
      );
    }
    return std::vector<Window>{Window{
      static_cast<std::uint64_t>(startSec * 1'000'000.0),
      static_cast<std::uint64_t>(clamped * 1'000'000.0),
    }};
  }

  if (shorterDurationUs < kFullComparisonBudgetUs) {
    return std::vector<Window>{Window{0, shorterDurationUs}};
  }

  auto windows = std::vector<Window>{};
  windows.reserve(kWindowCount);
  for (auto index = std::size_t{}; index < kWindowCount; ++index) {
    auto const startUs = static_cast<std::uint64_t>(
      static_cast<double>(index)
      * static_cast<double>(shorterDurationUs - kWindowDurationUs)
      / static_cast<double>(kWindowCount - 1)
    );
    windows.push_back(Window{startUs, kWindowDurationUs});
  }
  return windows;
}

// Single-input mode: probe the source for the chosen CQ, encode one segment
// per window with the production settings at that CQ, score each window and
// render the comparison against the segments.
auto renderPreview(
  appctx::AppContext& ctx,
  PreviewOptions const& options,
  fs::path const& originalPath,
  std::vector<fs::path> const& encodedPaths,
  FiltergraphSpec const& spec,
  fs::path const& outputPath
) -> eh::Result<int> {
  fs::create_directories(outputPath.parent_path());

  auto const cmd = buildPreviewCommand(
    ctx.toolchain.ffmpegPath.value_or(fs::path{"ffmpeg"}),
    originalPath,
    encodedPaths,
    spec,
    outputPath,
    PreviewEncoderSettings{
      .codec = ctx.config.videoCodec.value_or("hevc_nvenc"),
      .nvencPreset = ctx.config.nvencPreset,
    }
  );

  ExecResult result{};
  try {
    result = exec2(cmd);
  } catch (std::exception const& ex) {
    return eh::makeError("Preview generation could not be launched: {}", ex.what());
  }
  if (result.exitCode != 0 || !fs::exists(outputPath)) {
    return eh::makeError("Preview generation failed (exit code {}).", result.exitCode);
  }

  return 0;
}

void reportAndOpen(PreviewOptions const& options, fs::path const& outputPath) {
  terminal::println(Success, "Preview written to: {}", terminal::path(outputPath));
  if (!options.noOpen) {
    if (openfile::openWithDefaultApp(outputPath)) {
      terminal::println(Info, "Opened in the default player.");
    } else {
      terminal::println(Warning, "Could not open the preview in the default player.");
    }
  }
}

// Windows are independent: encode + score each one in parallel (bounded by
// the configured job count), then collect in order for the report.
struct WindowOutcome {
  videoquality::QualityMetric metric = videoquality::QualityMetric::Vmaf;
  std::optional<double> score;
};

struct WindowBatchResult {
  std::vector<WindowOutcome> outcomes;
  std::vector<fs::path> segments;
};

// The single preview progress bar: its context, handle, and the percent
// offset where the window phase starts (40 after a successful probe).
struct BarSlot {
  progress::ProgressContext& progressCtx;
  std::size_t bar;
  float windowBase;
};

// One window-encode batch for a single-input preview: the input, the
// windows to encode, the probe plan (CQ decision), the encode settings,
// and the temp dir for the segments.
struct WindowBatchSpec {
  fs::path const& original;
  std::vector<Window> const& windows;
  encodeprobe::ProbePlan const& plan;
  EncodeInputSettings const& settings;
  fs::path const& probeRoot;
};

// Encodes one probe window and scores it against the original segment.
// Returns the outcome (default on scoring failure) or an error on encode
// failure, which also flags windowEncodeFailed for the batch.
auto encodeAndScoreWindow(
  appctx::AppContext& ctx,
  fs::path const& original,
  EncodeInputSettings const& settings,
  fs::path const& segFile,
  Window const& window,
  int windowCq,
  std::size_t workers,
  std::atomic_bool& windowEncodeFailed
) -> eh::Result<WindowOutcome> {
  auto const ok = encodeprobe::runProbeEncode(
    ctx,
    original,
    settings,
    segFile,
    windowCq,
    encodeprobe::ProbeWindow{window.startUs, window.durationUs},
    workers
  );
  if (!ok) {
    windowEncodeFailed.store(true);
    return eh::makeError(
      "Preview window encode failed at {}us of {}",
      window.startUs,
      original.string()
    );
  }
  auto const ffmpeg = ctx.toolchain.ffmpegPath.value_or(fs::path{"ffmpeg"});
  auto const info =
    ctx.runtime.videoInfoCache.find(original).value_or(boost::json::value{});
  auto const scores = videoquality::measureSegmentQuality(
    videoquality::QualityRequest{
      .ffmpegPath = ffmpeg,
      .originalPath = original,
      .encodedPath = segFile,
      .startUs = window.startUs,
      .durationUs = window.durationUs,
      .originalVideoInfo = info,
      .encodedHasLocalPts = true,  // segments carry segment-local PTS
    }
  );
  if (!scores.has_value()) {
    LOG_WARN(
      "Preview scoring failed for window {}us: {}",
      window.startUs,
      scores.error()
    );
    return WindowOutcome{};
  }
  auto outcome = WindowOutcome{};
  outcome.metric = scores->metric;
  outcome.score = videoquality::percentile(scores->frameScores, 5.0);
  return outcome;
}

auto encodeAndScoreAllWindows(
  appctx::AppContext& ctx,
  WindowBatchSpec const& spec,
  BarSlot const& bars,
  std::atomic_bool& windowEncodeFailed
) -> WindowBatchResult {
  auto result = WindowBatchResult{};
  result.outcomes.resize(spec.windows.size());
  result.segments.resize(spec.windows.size());
  auto windowsCompleted = std::atomic_size_t{0};
  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(spec.windows.size());
  auto const previewWorkers = std::clamp<
    std::size_t
  >(ctx.config.maxParallelJobs.value_or(4), 1, spec.windows.size());
  for (auto index = std::size_t{}; index < spec.windows.size(); ++index) {
    auto const segFile = spec.probeRoot / std::format("win{}.ts", index);
    result.segments[index] = segFile;
    tasks.push_back(
      {.id = std::format("preview-window:{}", index),
       .label = std::format("window {}", index),
       .input = spec.original.string(),
       // NOLINTNEXTLINE(bugprone-exception-escape): taskexec::runTasks catches
       .run = [&, index, segFile](taskexec::TaskContext&) -> eh::Result<void> {
         auto outcome = encodeAndScoreWindow(
           ctx,
           spec.original,
           spec.settings,
           segFile,
           spec.windows[index],
           ctx.config.crf.value_or(spec.plan.chosenCq),
           previewWorkers,
           windowEncodeFailed
         );
         if (!outcome) { return std::unexpected(outcome.error()); }
         result.outcomes[index] = *outcome;
         auto const done = windowsCompleted.fetch_add(1) + 1;
         bars.progressCtx.setProgress(
           bars.bar,
           bars.windowBase
             + (85.0f - bars.windowBase)
               * static_cast<float>(done)
               / static_cast<float>(spec.windows.size())
         );
         bars.progressCtx.setPostfixText(
           bars.bar,
           std::format("Encoding windows: {}/{}", done, spec.windows.size())
         );
         return {};
       }}
    );
  }
  taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = previewWorkers,
    .progress = nullptr,
    .hideCursor = false,
  });
  return result;
}

// Copies the scored metrics back into the windows and returns the worst index.
auto findWorstWindow(
  std::vector<Window>& windows,
  std::vector<WindowOutcome> const& outcomes
) -> std::optional<std::size_t> {
  auto worstIndex = std::optional<std::size_t>{};
  auto worstScore = std::optional<double>{};
  for (auto index = std::size_t{}; index < windows.size(); ++index) {
    auto& window = windows[index];
    window.metric = outcomes[index].metric;
    window.score = outcomes[index].score;
    if (
      window.score.has_value()
      && (!worstScore.has_value() || window.score.value() < worstScore.value())
    ) {
      worstScore = window.score;
      worstIndex = index;
    }
  }
  return worstIndex;
}

// Removes the per-run probe dir; retries briefly because a just-exited child
// (scoring/encode) may still hold a transient handle on Windows.
struct PreviewProbeRootGuard {
  fs::path root;
  ~PreviewProbeRootGuard() {  // NOLINT(bugprone-exception-escape): error_code overloads never throw
    for (auto attempt = 0; attempt < 3; ++attempt) {
      auto ec = std::error_code{};
      fs::remove_all(root, ec);
      if (!ec) { return; }
      std::this_thread::sleep_for(std::chrono::milliseconds{200});
    }
  }
};

auto createPreviewProbeRoot() -> eh::Result<fs::path> {
  auto probeRoot = workdirs::scratchDir() / std::format("preview_{}", getUUID());
  auto ec = std::error_code{};
  fs::create_directories(probeRoot, ec);
  if (ec) {
    return eh::makeError(
      "Failed to create preview temp directory: {} ({})",
      probeRoot.string(),
      ec.message()
    );
  }
  return probeRoot;
}

// Renders the comparison, drives the bar to completion, then prints the
// summary and opens the player.
auto renderAndReportSingleInput(
  appctx::AppContext& ctx,
  PreviewOptions const& options,
  VideoProbe const& original,
  std::vector<Window> windows,
  std::vector<fs::path> const& segments,
  fs::path const& outputPath,
  BarSlot const& bars,
  std::optional<std::size_t> worstIndex
) -> eh::Result<int> {
  auto const spec = FiltergraphSpec{
    .original = original,
    .encoded = original,
    .windows = windows,  // copy: the window list is part of the post-render summary
    .encodedWindowsAreSegments = true,
  };
  auto const renderResult =
    renderPreview(ctx, options, options.original, segments, spec, outputPath);
  if (!renderResult) {
    bars.progressCtx.setTone(bars.bar, progress::Tone::Failure);
    bars.progressCtx.setPostfixText(bars.bar, "Preview generation failed");
    return renderResult;
  }
  bars.progressCtx.setProgress(bars.bar, 100.0f);
  bars.progressCtx.setTone(bars.bar, progress::Tone::Success);
  bars.progressCtx.setPostfixText(bars.bar, "Preview complete");

  // Summary output only after the render finished.
  printWindows(windows, worstIndex);
  reportAndOpen(options, outputPath);
  return renderResult;
}

// Probes the original for a single-input preview: drives the progress bar
// through the probe phases and returns the plan plus the bar's window base.
auto probeSingleInputPlan(
  appctx::AppContext& ctx,
  PreviewOptions const& options,
  fs::path const& probeRoot,
  BarSlot const& bars,
  std::string const& fileName
) -> std::pair<encodeprobe::ProbePlan, float> {
  auto step = std::size_t{0};
  auto const onStep = [&](int cq, std::string_view phase) {
    ++step;
    bars.progressCtx.setProgress(
      bars.bar,
      40.0f * static_cast<float>(step) / static_cast<float>(encodeprobe::kMaxProbeSteps)
    );
    bars.progressCtx.setPostfixText(
      bars.bar,
      std::format("Probing: {} · CQ {} {}", fileName, cq, phase)
    );
  };
  auto const onPoint = [&](std::size_t done, int cq) {
    bars.progressCtx.setProgress(
      bars.bar,
      40.0f
        * static_cast<float>(done * encodeprobe::kStepsPerProbePoint)
        / static_cast<float>(encodeprobe::kMaxProbeSteps)
    );
    bars.progressCtx
      .setPostfixText(bars.bar, std::format("Probing: {} · CQ {} scored", fileName, cq));
  };
  auto const plan =
    encodeprobe::probeSingleFile(ctx, options.original, probeRoot, 1, onPoint, onStep);
  auto windowBase = 0.0f;
  if (plan.probed) {
    windowBase = 40.0f;
    bars.progressCtx.setPostfixText(
      bars.bar,
      std::format("Probed: {} (CQ {})", fileName, plan.chosenCq)
    );
  } else {
    bars.progressCtx.setPostfixText(
      bars.bar,
      std::format(
        "Probing skipped: {} (default CQ {})",
        fileName,
        encodeprobe::kDefaultCq
      )
    );
  }
  if (!plan.probed) {
    terminal::println(
      Warning,
      "Probing skipped (short video or scoring failure); previewing at default CQ {}.",
      encodeprobe::kDefaultCq
    );
  }
  return {plan, windowBase};
}

auto runSingleInput(
  appctx::AppContext& ctx,
  PreviewOptions const& options,
  VideoProbe const& original,
  std::vector<Window> windows,
  fs::path const& outputPath
) -> eh::Result<int> {
  auto const probeRoot = createPreviewProbeRoot();
  if (!probeRoot) { return eh::makeError("{}", probeRoot.error()); }
  PreviewProbeRootGuard rootGuard{probeRoot.value()};

  auto progressCtx = progress::ProgressContext{};
  auto cursorGuard = progress::CursorGuard{};
  auto const fileName = options.original.filename().string();
  // One bar spans the whole pipeline: probe 0-40%, windows 40-85%, render 85-100%.
  auto const bar =
    progressCtx.addBar(std::format("Previewing: {}", fileName), progress::Tone::Active);
  auto const probeSlot = BarSlot{progressCtx, bar, 0.0f};
  auto const [plan, windowBase] =
    probeSingleInputPlan(ctx, options, *probeRoot, probeSlot, fileName);
  auto const windowBars = BarSlot{progressCtx, bar, windowBase};

  auto const settings = resolveInputEncodeSettings(
    ctx.toolchain,
    ctx.runtime,
    options.original,
    ctx.config.nvencPreset
  );

  auto windowEncodeFailed = std::atomic_bool{false};
  auto const windowBatch = encodeAndScoreAllWindows(
    ctx,
    WindowBatchSpec{
      .original = options.original,
      .windows = windows,
      .plan = plan,
      .settings = settings,
      .probeRoot = *probeRoot,
    },
    windowBars,
    windowEncodeFailed
  );
  if (windowEncodeFailed.load()) {
    progressCtx.setTone(bar, progress::Tone::Failure);
    progressCtx.setPostfixText(bar, "Window encode failed");
    return eh::makeError("Preview window encode failed.");
  }
  progressCtx.setPostfixText(
    bar,
    std::format("Windows encoded: {}/{}", windows.size(), windows.size())
  );

  if (stopsignal::isStopRequested()) {
    return eh::makeError("Preview canceled by user.");
  }

  auto worstIndex = findWorstWindow(windows, windowBatch.outcomes);

  progressCtx.setProgress(bar, 85.0f);
  progressCtx.setPostfixText(bar, "Rendering comparison video...");
  auto const renderResult = renderAndReportSingleInput(
    ctx,
    options,
    original,
    windows,
    windowBatch.segments,
    outputPath,
    windowBars,
    worstIndex
  );
  return renderResult;
}

// Runs the comparison render: build the command, execute, report and open.
namespace {

auto resolveManualRange(PreviewOptions const& options)
  -> std::optional<std::pair<double, double>> {
  if (options.startSeconds.has_value() || options.durationSeconds.has_value()) {
    return std::pair{
      options.startSeconds.value_or(0.0),
      options.durationSeconds.value_or(0.0),
    };
  }
  return std::nullopt;
}

auto resolvePreviewOutputPath(PreviewOptions const& options) -> eh::Result<fs::path> {
  auto outputPath = options.output.has_value() ? options.output.value()
                                               : options.original.parent_path()
      / std::format("{}.preview.mp4", options.original.stem().string());
  if (
    outputPath == options.original
    || (options.encoded.has_value() && outputPath == options.encoded.value())
  ) {
    return eh::makeError(
      "Preview output would overwrite an input file: {}",
      outputPath.string()
    );
  }
  return outputPath;
}

// Scores every window of the comparison and returns the index of the worst.
// bars (optional) spans the scoring phase: windowBase→85% by scored count.
auto scoreComparisonWindows(
  appctx::AppContext& ctx,
  PreviewOptions const& options,
  VideoProbe const& original,
  std::vector<Window>& windows,
  BarSlot const* bars
) -> std::optional<std::size_t> {
  auto const ffmpeg = ctx.toolchain.ffmpegPath.value_or(fs::path{"ffmpeg"});
  auto const info =
    ctx.runtime.videoInfoCache.find(options.original).value_or(boost::json::value{});
  auto worstIndex = std::optional<std::size_t>{};
  auto worstScore = std::optional<double>{};
  for (auto index = std::size_t{}; index < windows.size(); ++index) {
    auto& window = windows[index];
    auto const scores = videoquality::measureSegmentQuality(
      videoquality::QualityRequest{
        .ffmpegPath = ffmpeg,
        .originalPath = options.original,
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): caller only scores windows in comparison mode
        .encodedPath = options.encoded.value(),
        .startUs = window.startUs,
        .durationUs = window.durationUs,
        .originalVideoInfo = info,
      }
    );
    if (scores.has_value()) {
      window.metric = scores->metric;
      window.score = videoquality::percentile(scores->frameScores, 5.0);
    } else {
      LOG_WARN(
        "Preview scoring failed for window {}us: {}",
        window.startUs,
        scores.error()
      );
    }
    if (bars) {
      auto const done = index + 1;
      bars->progressCtx.setProgress(
        bars->bar,
        bars->windowBase
          + (85.0f - bars->windowBase)
            * static_cast<float>(done)
            / static_cast<float>(windows.size())
      );
      bars->progressCtx.setPostfixText(
        bars->bar,
        std::format("Scoring windows: {}/{}", done, windows.size())
      );
    }
    if (
      window.score.has_value()
      && (!worstScore.has_value() || window.score.value() < worstScore.value())
    ) {
      worstScore = window.score;
      worstIndex = index;
    }
  }
  return worstIndex;
}

}  // namespace

// Two-input comparison mode: one bar spans probe (0-10%), window scoring
// (10-85%), render (85-100%). Scoring is sequential, so per-window updates
// are exact; the render jumps to completion like the single-input mode.
auto runTwoInput(
  appctx::AppContext& ctx,
  PreviewOptions const& options,
  fs::path const& outputPath
) -> eh::Result<int> {
  auto progressCtx = progress::ProgressContext{};
  auto cursorGuard = progress::CursorGuard{};
  auto const fileName = options.original.filename().string();
  auto const bar =
    progressCtx.addBar(std::format("Previewing: {}", fileName), progress::Tone::Active);

  // run() dispatched here only after validating both inputs exist.
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  auto const& encodedPath = options.encoded.value();

  progressCtx.setPostfixText(bar, std::format("Probing: {}", fileName));
  auto const originalRes = probeVideo(ctx.toolchain, ctx.runtime, options.original);
  if (!originalRes) { return eh::makeError("{}", originalRes.error()); }
  auto const& original = originalRes.value();
  if (original.durationUs == 0) {
    return eh::makeError(
      "Cannot preview a video with zero duration: {}",
      options.original.string()
    );
  }

  auto const encodedName = encodedPath.filename().string();
  progressCtx.setProgress(bar, 5.0f);
  progressCtx.setPostfixText(bar, std::format("Probing: {}", encodedName));
  auto const encodedRes = probeVideo(ctx.toolchain, ctx.runtime, encodedPath);
  if (!encodedRes) { return eh::makeError("{}", encodedRes.error()); }
  auto const& encoded = encodedRes.value();
  if (encoded.durationUs == 0) {
    return eh::makeError(
      "Cannot preview a video with zero duration: {}",
      encodedPath.string()
    );
  }

  progressCtx.setProgress(bar, 10.0f);
  auto const manualRange = resolveManualRange(options);
  auto windowsRes =
    pickPreviewWindows(std::min(original.durationUs, encoded.durationUs), manualRange);
  if (!windowsRes) { return eh::makeError("{}", windowsRes.error()); }
  auto windows = std::move(windowsRes.value());

  auto worstIndex = std::optional<std::size_t>{};
  if (!manualRange.has_value()) {
    auto const scoringBars = BarSlot{progressCtx, bar, 10.0f};
    worstIndex = scoreComparisonWindows(ctx, options, original, windows, &scoringBars);
  }

  progressCtx.setProgress(bar, 85.0f);
  progressCtx.setPostfixText(bar, "Rendering comparison video...");
  auto const spec = FiltergraphSpec{
    .original = original,
    .encoded = encoded,
    .windows = windows,  // copy: the window list is part of the post-render summary
  };
  auto const renderResult =
    renderPreview(ctx, options, options.original, {encodedPath}, spec, outputPath);
  if (!renderResult) {
    progressCtx.setTone(bar, progress::Tone::Failure);
    progressCtx.setPostfixText(bar, "Preview generation failed");
    return renderResult;
  }
  progressCtx.setProgress(bar, 100.0f);
  progressCtx.setTone(bar, progress::Tone::Success);
  progressCtx.setPostfixText(bar, "Preview complete");

  printWindows(windows, worstIndex);
  reportAndOpen(options, outputPath);
  return 0;
}

auto run(appctx::AppContext& ctx, PreviewOptions const& options) -> eh::Result<int> {
  if (!fs::is_regular_file(options.original)) {
    return eh::makeError("Preview input does not exist: {}", options.original.string());
  }
  if (options.encoded.has_value() && !fs::is_regular_file(options.encoded.value())) {
    return eh::makeError(
      "Preview input does not exist: {}",
      options.encoded.value().string()
    );
  }

  auto const outputPath = resolvePreviewOutputPath(
    options
  );  // NOLINT(performance-no-automatic-move): read multiple times; const is intentional
  if (!outputPath) { return eh::makeError("{}", outputPath.error()); }

  if (options.encoded.has_value()) {
    return runTwoInput(ctx, options, outputPath.value());
  }

  auto const originalRes = probeVideo(ctx.toolchain, ctx.runtime, options.original);
  if (!originalRes) { return eh::makeError("{}", originalRes.error()); }
  auto const& original = originalRes.value();
  if (original.durationUs == 0) {
    return eh::makeError(
      "Cannot preview a video with zero duration: {}",
      options.original.string()
    );
  }

  auto windowsRes = pickPreviewWindows(original.durationUs, resolveManualRange(options));
  if (!windowsRes) { return eh::makeError("{}", windowsRes.error()); }
  return runSingleInput(
    ctx,
    options,
    original,
    std::move(windowsRes.value()),
    outputPath.value()
  );
}

}  // namespace preview
