#include "video/encode_probe.h"

#include "core/collision_naming.h"
#include "core/display_text.h"
#include "core/progress.h"
#include "core/task_executor.h"
#include "infra/console_width.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "utils/utils.h"
#include "video/probe_cache.h"
#include "video/video_info.h"
#include "video/video_quality.h"
#include "video/video_workflow_utils.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::VIDEO_PROBE);

namespace fs = std::filesystem;
using enum terminal::MessageKind;

namespace encodeprobe {

namespace {

auto floorForMetric(videoquality::QualityMetric metric, int vmafFloor) -> double {
  using enum videoquality::QualityMetric;
  switch (metric) {
    case Vmaf : return static_cast<double>(vmafFloor);
    case Ssim : return videoquality::ssimFloorForVmafFloor(vmafFloor);
    case Xpsnr: return videoquality::xpsnrFloorForVmafFloor(vmafFloor);
  }
  return static_cast<double>(vmafFloor);
}

bool meetsFloor(ProbePoint const& point, int vmafFloor) {
  return point.p5 >= floorForMetric(point.metric, vmafFloor);
}

auto measurePoint(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  EncodeInputSettings const& settings,
  fs::path const& probeDir,
  int cq,
  std::pair<ProbeWindow, ProbeWindow> const& windows,
  std::size_t workerCount,
  ProbeStepCallback const& onStep = {}
) -> std::optional<ProbePoint> {
  // Segments are per-video: parallel probes of different inputs share
  // probeDir, so the file name carries a per-input key.
  auto const vidKey = fs::hash_value(inputPath);
  auto const segA = probeDir / std::format("cq{}_{}_{}.ts", vidKey, cq, 0);
  auto const segB = probeDir / std::format("cq{}_{}_{}.ts", vidKey, cq, 1);
  if (onStep) { onStep(cq, "encode 1/2"); }
  if (!runProbeEncode(ctx, inputPath, settings, segA, cq, windows.first, workerCount)) {
    return std::nullopt;
  }
  if (onStep) { onStep(cq, "encode 2/2"); }
  if (!runProbeEncode(ctx, inputPath, settings, segB, cq, windows.second, workerCount)) {
    return std::nullopt;
  }

  auto const ffmpeg = ctx.toolchain.ffmpegPath.value_or(fs::path{"ffmpeg"});
  auto const info =
    ctx.runtime.videoInfoCache.find(inputPath).value_or(boost::json::value{});
  if (onStep) { onStep(cq, "score 1/2"); }
  auto const scoresA = videoquality::measureSegmentQuality(
    videoquality::QualityRequest{
      .ffmpegPath = ffmpeg,
      .originalPath = inputPath,
      .encodedPath = segA,
      .startUs = windows.first.startUs,
      .durationUs = windows.first.durationUs,
      .originalVideoInfo = info,
      .encodedHasLocalPts = true,  // probe segments carry segment-local PTS
    }
  );
  if (onStep) { onStep(cq, "score 2/2"); }
  auto const scoresB = videoquality::measureSegmentQuality(
    videoquality::QualityRequest{
      .ffmpegPath = ffmpeg,
      .originalPath = inputPath,
      .encodedPath = segB,
      .startUs = windows.second.startUs,
      .durationUs = windows.second.durationUs,
      .originalVideoInfo = info,
      .encodedHasLocalPts = true,  // probe segments carry segment-local PTS
    }
  );
  if (!scoresA.has_value() || !scoresB.has_value()) {
    LOG_WARN(
      "Probe scoring failed for {} at cq={}: {}",
      inputPath.string(),
      cq,
      scoresA.has_value() ? scoresB.error() : scoresA.error()
    );
    return std::nullopt;
  }

  // The two windows pool into one percentile: mixing scales (e.g. one window
  // degraded XPSNR→VMAF asymmetrically) is meaningless, so discard the point.
  if (scoresA->metric != scoresB->metric) {
    LOG_WARN(
      "Probe windows scored with different metrics for {} at cq={} ({}/{}); discarding "
      "point",
      inputPath.string(),
      cq,
      videoquality::metricName(scoresA->metric),
      videoquality::metricName(scoresB->metric)
    );
    return std::nullopt;
  }

  auto allFrames = scoresA->frameScores;
  allFrames
    .insert(allFrames.end(), scoresB->frameScores.begin(), scoresB->frameScores.end());
  auto const p5 = videoquality::percentile(allFrames, 5.0);
  if (!p5.has_value()) { return std::nullopt; }

  auto ec = std::error_code{};
  auto const bytes = fs::file_size(segA, ec) + fs::file_size(segB, ec);
  return ProbePoint{cq, p5.value(), scoresA->metric, bytes};
}

double audioBitrateBps(boost::json::value const& vidInfo) {
  if (!vidInfo.is_object()) { return 0.0; }
  auto const streamsIt = vidInfo.as_object().find("streams");
  if (streamsIt == vidInfo.as_object().end() || !streamsIt->value().is_array()) {
    return 0.0;
  }
  for (auto const& streamVal: streamsIt->value().as_array()) {
    if (!streamVal.is_object()) { continue; }
    auto const& stream = streamVal.as_object();
    auto const codecTypeIt = stream.find("codec_type");
    if (
      codecTypeIt == stream.end()
      || !codecTypeIt->value().is_string()
      || codecTypeIt->value().as_string() != "audio"
    ) {
      continue;
    }
    if (
      auto const bitRateIt = stream.find("bit_rate");
      bitRateIt != stream.end() && bitRateIt->value().is_int64()
    ) {
      return static_cast<double>(bitRateIt->value().as_int64());
    }
  }
  return 0.0;
}

}  // namespace

auto probeSingleFile(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  fs::path const& probeDir,
  std::size_t workerCount,
  ProbePointCallback const& onPoint,
  ProbeStepCallback const& onStep
) -> ProbePlan {
  auto plan = ProbePlan{};
  plan.inputPath = inputPath;

  auto const durationRes = getVidTotalDurationUs(ctx.toolchain, ctx.runtime, inputPath);
  if (!durationRes.has_value()) {
    LOG_DEBUG("Probing skipped (no duration): {}", inputPath.string());
    return plan;
  }
  auto const totalDurationUs = durationRes.value();
  auto const windows = pickProbeWindows(totalDurationUs);
  if (!windows.has_value()) {
    LOG_DEBUG("Probing skipped (short video): {}", inputPath.string());
    return plan;
  }

  auto const settings = resolveInputEncodeSettings(
    ctx.toolchain,
    ctx.runtime,
    inputPath,
    ctx.config.nvencPreset
  );
  // Used for the bitrate estimate below; measurePoint re-resolves it from the
  // same cache when scoring.
  auto const info =
    ctx.runtime.videoInfoCache.find(inputPath).value_or(boost::json::value{});

  auto const points = probeCqSequence(
    [&](int cq) -> std::optional<ProbePoint> {
      return measurePoint(
        ctx,
        inputPath,
        settings,
        probeDir,
        cq,
        windows.value(),
        workerCount,
        onStep
      );
    },
    ctx.config.minVmaf,
    onPoint
  );
  if (!points.has_value()) {
    LOG_WARN(
      "Probing failed for {}; using default CQ {}",
      inputPath.string(),
      kDefaultCq
    );
    return plan;
  }

  auto const decision = decideCq(points.value(), ctx.config.minVmaf);
  plan.probed = true;
  plan.metric = points.value().front().metric;
  plan.chosenCq = decision.cq;
  plan.p5 = decision.p5;
  plan.unreachableFloor = decision.unreachableFloor;
  plan.estimatedBytes = static_cast<std::uintmax_t>(
    (decision.videoBitrateBps + audioBitrateBps(info))
    * static_cast<double>(totalDurationUs)
    / 8.0
    / 1'000'000.0
  );
  return plan;
}

bool runProbeEncode(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  EncodeInputSettings const& settings,
  fs::path const& segFile,
  int cq,
  ProbeWindow const& window,
  std::size_t workerCount
) {
  auto const cfg = buildProbeSegmentConfig(
    ctx.toolchain,
    SegmentEncodeSpec{
      .inputPath = inputPath,
      .segmentIndex = 0,
      .startUs = window.startUs,
      .durationUs = window.durationUs,
      .tempOutputPath = segFile,
    },
    EncodeProfile{
      .outputFormat = ctx.config.outputFormat,
      .videoCodec = ctx.config.videoCodec,
      .settings = settings,
      .workerCount = workerCount,
    },
    cq
  );

  auto ec = std::error_code{};
  fs::remove(segFile, ec);

  ExecResult result{};
  try {
    result = exec2(cfg.buildCMD());
  } catch (std::exception const& ex) {
    LOG_WARN("Probe encode could not be launched (cq={}): {}", cq, ex.what());
    return false;
  }
  if (result.exitCode != 0 || !fs::exists(segFile)) {
    LOG_WARN(
      "Probe encode failed: input={} cq={} exitCode={}",
      inputPath.string(),
      cq,
      result.exitCode
    );
    return false;
  }
  return true;
}

auto pickProbeWindows(std::uint64_t totalDurationUs)
  -> std::optional<std::pair<ProbeWindow, ProbeWindow>> {
  if (totalDurationUs < kProbeBudgetUs) { return std::nullopt; }

  auto const startA =
    static_cast<std::uint64_t>(static_cast<double>(totalDurationUs) * 0.25);
  auto const startB =
    static_cast<std::uint64_t>(static_cast<double>(totalDurationUs) * 0.75);
  return std::pair{
    ProbeWindow{startA, kProbeWindowDurationUs},
    ProbeWindow{startB, kProbeWindowDurationUs},
  };
}

auto decideCq(std::span<ProbePoint const> points, int vmafFloor) -> ProbeDecision {
  // Sort by cq: extension points arrive in probe order, not cq order.
  auto sorted = std::vector<ProbePoint>{points.begin(), points.end()};
  std::ranges::sort(sorted, {}, &ProbePoint::cq);

  // Find the last point meeting the floor.
  auto lastMet = -1;
  for (auto index = std::size_t{0}; index < sorted.size(); ++index) {
    if (meetsFloor(sorted[index], vmafFloor)) { lastMet = static_cast<int>(index); }
  }

  auto const bitrateOf = [](ProbePoint const& point) {
    // segmentBytes covers two 10s probe windows.
    auto const measuredUs = 2 * kProbeWindowDurationUs;
    return static_cast<double>(point.segmentBytes)
      * 8.0
      / static_cast<double>(measuredUs)
      * 1'000'000.0;
  };

  if (lastMet == -1) {
    auto const& point = sorted.front();
    return ProbeDecision{
      .cq = point.cq,
      .p5 = point.p5,
      .videoBitrateBps = bitrateOf(point),
      .unreachableFloor = true,
    };
  }

  auto const lastIndex = static_cast<std::size_t>(lastMet);
  if (lastIndex == sorted.size() - 1) {
    auto const& point = sorted.back();
    return ProbeDecision{
      .cq = point.cq,
      .p5 = point.p5,
      .videoBitrateBps = bitrateOf(point),
      .unreachableFloor = false,
    };
  }

  auto const& low = sorted[lastIndex];
  auto const& high = sorted[lastIndex + 1];
  auto const floor = floorForMetric(low.metric, vmafFloor);
  auto const scoreSpan = low.p5 - high.p5;
  auto const t = scoreSpan > 0.0 ? (low.p5 - floor) / scoreSpan : 0.0;
  auto const cq = static_cast<int>(
    std::floor(static_cast<double>(low.cq) + t * static_cast<double>(high.cq - low.cq))
  );
  auto const p5 = low.p5 + t * (high.p5 - low.p5);
  return ProbeDecision{
    .cq = cq,
    .p5 = p5,
    .videoBitrateBps = bitrateOf(low) + t * (bitrateOf(high) - bitrateOf(low)),
    .unreachableFloor = false,
  };
}

namespace {

void recordProbePoint(
  std::vector<ProbePoint>& points,
  ProbePointCallback const& onPoint,
  ProbePoint point
) {
  auto const cq = point.cq;
  points.push_back(point);
  if (onPoint) { onPoint(points.size(), cq); }
}

// Wave 1: base CQs are independent — measure them in parallel. The result
// is identical to serial order (collected in cq order); only the wall time
// shrinks. Extension points stay serial: each depends on the previous.
bool probeBaseCqs(
  ProbeMeasure const& measure,
  std::vector<ProbePoint>& points,
  ProbePointCallback const& onPoint
) {
  auto baseResults = std::vector<std::optional<ProbePoint>>(kBaseCqs.size());
  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(kBaseCqs.size());
  for (auto index = std::size_t{}; index < kBaseCqs.size(); ++index) {
    tasks.push_back({
      .id = std::format("probe-cq:{}", kBaseCqs[index]),
      .label = std::format("cq {}", kBaseCqs[index]),
      .input = std::format("{}", kBaseCqs[index]),
      .run = [&, index](taskexec::TaskContext&) -> eh::Result<void> {
        baseResults[index] = measure(kBaseCqs[index]);
        return {};
      },
    });
  }
  taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = kBaseCqs.size(),
    .progress = nullptr,
    .hideCursor = false,
  });
  for (auto& baseResult: baseResults) {
    if (!baseResult.has_value()) { return false; }
    recordProbePoint(points, onPoint, baseResult.value());
  }
  return true;
}

// Probes one edge of the CQ range: the given first point, then the bound
// itself when the floor status there matches stopWhenMet (low side: probe
// down while the floor is unmet; high side: probe up while it is met).
bool probeSide(
  ProbeMeasure const& measure,
  std::vector<ProbePoint>& points,
  ProbePointCallback const& onPoint,
  int vmafFloor,
  int firstCq,
  int edgeCq,
  bool stopWhenMet
) {
  if (auto const point = measure(firstCq); point.has_value()) {
    recordProbePoint(points, onPoint, point.value());
    if (meetsFloor(points.back(), vmafFloor) == stopWhenMet) {
      if (auto const edge = measure(edgeCq); edge.has_value()) {
        recordProbePoint(points, onPoint, edge.value());
      } else {
        return false;
      }
    }
  } else {
    return false;
  }
  return true;
}

}  // namespace

auto probeCqSequence(
  ProbeMeasure const& measure,
  int vmafFloor,
  ProbePointCallback const& onPoint
) -> std::optional<std::vector<ProbePoint>> {
  auto points = std::vector<ProbePoint>{};
  if (!probeBaseCqs(measure, points, onPoint)) { return std::nullopt; }

  if (!meetsFloor(points.front(), vmafFloor)) {
    if (
      !probeSide(measure, points, onPoint, vmafFloor, kMinCq + kCqStep, kMinCq, false)
    ) {
      return std::nullopt;
    }
  } else if (meetsFloor(points.back(), vmafFloor)) {
    if (!probeSide(measure, points, onPoint, vmafFloor, kMaxCq - kCqStep, kMaxCq, true)) {
      return std::nullopt;
    }
  }

  return points;
}

auto buildProbeSegmentConfig(
  appctx::ToolchainPaths const& toolchain,
  SegmentEncodeSpec const& spec,
  EncodeProfile const& profile,
  int cq
) -> EncodeConfig {
  auto probeProfile = profile;
  probeProfile.crf = cq;
  return buildSegmentEncodeConfig(toolchain, spec, probeProfile);
}

// One probe task: drives the slot bar for the file, forwards step/point
// callbacks to the overall bar, and stores the resulting plan.
// Removes the per-run probe dir; retries because a just-exited child
// (scoring/encode) may still hold a transient handle on Windows.
auto createProbeRoot() -> eh::Result<fs::path> {
  return videoworkflow::createScratchProbeRoot("probe", "probe");
}

// Progress plumbing shared by probe tasks: bar registry, per-slot bars and
// progress cells, the completed counter, and the overall-bar updater. One
// instance per probe phase, constructed flat in runProbePhase.
struct ProbeProgress {
  progress::ProgressContext& progressCtx;
  std::span<std::size_t const> slotBars;
  std::vector<std::atomic<float>>& slotProgress;
  std::atomic_size_t& completed;
  std::function<void()> const& updateOverall;
};

auto initSlotBars(progress::ProgressContext& progressCtx, std::size_t workerCount)
  -> std::vector<std::size_t> {
  auto slotBars = std::vector<std::size_t>(workerCount);
  for (auto slot = std::size_t{}; slot < workerCount; ++slot) {
    slotBars[slot] =
      progressCtx
        .addBar(std::format("Probing: [idle-{}]", slot + 1), progress::Tone::Idle);
  }
  return slotBars;
}

auto buildProbeTaskSpec(
  appctx::AppContext& ctx,
  std::span<fs::path const> vids,
  std::size_t index,
  fs::path const& probeRoot,
  ProbeProgress const& progress,
  std::vector<ProbePlan>& plans,
  std::size_t workerCount
) -> taskexec::TaskSpec {
  auto const fileName = vids[index].filename().string();
  return taskexec::TaskSpec{
    .id = std::format("probe:{}", collisionnaming::stablePathString(vids[index])),
    .label = fileName,
    .input = vids[index].string(),
.run = [&, index, fileName, vids, progress](  // NOLINT(bugprone-exception-escape): taskexec::runTasks catches
  // vids captured by value: a cheap POD copy that would dangle otherwise
  taskexec::TaskContext& taskCtx) -> eh::Result<void> {
  auto const slot = taskCtx.slot;
  auto const barIndex = progress.slotBars[slot];
  progress.progressCtx.setTone(barIndex, progress::Tone::Active);
  progress.progressCtx.resetEta(barIndex);
  progress.progressCtx.setProgress(barIndex, 0.0f);
  progress.progressCtx.setPostfixText(barIndex, std::format("Probing: {}", fileName));
  auto step = std::size_t{0};
  auto const onStep = [&, barIndex, slot](int cq, std::string_view phase) {
    ++step;
    auto const p =
      100.0f * static_cast<float>(step) / static_cast<float>(kMaxProbeSteps);
    progress.slotProgress[slot].store(p);
    progress.progressCtx.setProgress(barIndex, p);
    progress.progressCtx.setPostfixText(
      barIndex,
      std::format("Probing: {} · CQ {} {}", fileName, cq, phase)
    );
    progress.updateOverall();
  };
  auto const onPoint = [&, barIndex, slot](std::size_t done, int cq) {
    auto const p = 100.0f
      * static_cast<float>(done * kStepsPerProbePoint)
      / static_cast<float>(kMaxProbeSteps);
    progress.slotProgress[slot].store(p);
    progress.progressCtx.setProgress(barIndex, p);
    progress.progressCtx.setPostfixText(
      barIndex,
      std::format("Probing: {} · CQ {} scored", fileName, cq)
    );
    progress.updateOverall();
  };
  plans[index] =
    probeSingleFile(ctx, vids[index], probeRoot, workerCount, onPoint, onStep);
  auto const& plan = plans[index];
  if (plan.probed) {
    progress.progressCtx.setProgress(barIndex, 100.0f);
    progress.progressCtx.setTone(barIndex, progress::Tone::Success);
    progress.progressCtx.setPostfixText(
      barIndex,
      std::format("Probed: {} (CQ {})", fileName, plan.chosenCq)
    );
  } else {
    progress.progressCtx.setTone(barIndex, progress::Tone::Idle);
    progress.progressCtx.setPostfixText(
      barIndex,
      std::format("Skipped: {} (default CQ {})", fileName, kDefaultCq)
    );
  }
  progress.slotProgress[slot].store(0.0f);
  progress.completed.fetch_add(1);
  progress.updateOverall();
  return {};
}
  };
}

namespace {

auto metricToString(videoquality::QualityMetric metric) -> std::string {
  return std::string{videoquality::metricName(metric)};
}

auto metricFromString(std::string_view metric) -> videoquality::QualityMetric {
  using enum videoquality::QualityMetric;
  if (metric == "SSIM") { return Ssim; }
  if (metric == "XPSNR") { return Xpsnr; }
  return Vmaf;
}

// Cache key for the decision inputs of inputPath; nullopt when the file cannot
// be stat'ed or its preset is unresolved (no dimensions), which probing would
// also have to measure anyway.
auto cacheKeyForInput(appctx::AppContext& ctx, fs::path const& inputPath)
  -> std::optional<std::string> {
  auto ec = std::error_code{};
  auto const fileSize = fs::file_size(inputPath, ec);
  if (ec) { return std::nullopt; }
  auto const mtimeMs = probecache::lastWriteTimeMs(inputPath);
  if (mtimeMs == 0) { return std::nullopt; }

  auto const codec = ctx.config.videoCodec.value_or("hevc_nvenc");
  auto const settings = resolveInputEncodeSettings(
    ctx.toolchain,
    ctx.runtime,
    inputPath,
    ctx.config.nvencPreset
  );
  if (!settings.nvencPreset.has_value()) { return std::nullopt; }

  auto const vidInfo = ctx.runtime.videoInfoCache.find(inputPath);
  auto const metric = videoquality::isHdrVideo(vidInfo.value_or(boost::json::value{}))
    ? std::string_view{"SSIM"}
    : std::string_view{"XPSNR"};

  return probecache::probeCacheKey(
    inputPath,
    fileSize,
    mtimeMs,
    codec,
    settings.nvencPreset,
    settings.maxrateKbps,
    ctx.config.minVmaf,
    metric
  );
}

auto planFromCache(probecache::Entry const& entry, fs::path const& inputPath)
  -> ProbePlan {
  auto plan = ProbePlan{};
  plan.inputPath = inputPath;
  plan.probed = true;
  plan.fromCache = true;
  plan.chosenCq = entry.chosenCq;
  plan.p5 = entry.p5;
  plan.estimatedBytes = entry.estimatedBytes;
  plan.metric = metricFromString(entry.metric);
  plan.unreachableFloor = entry.unreachableFloor;
  return plan;
}

// Persist fresh decisions so the next run skips probing (single writer at the
// end of the phase; never written for skipped/probed==false plans).
void flushCachePlans(
  appctx::AppContext& ctx,
  std::span<fs::path const> vids,
  std::vector<ProbePlan> const& plans
) {
  auto updates = std::vector<probecache::Entry>{};
  for (auto index = std::size_t{0}; index < vids.size(); ++index) {
    auto const& plan = plans[index];
    if (!plan.probed || plan.fromCache) { continue; }
    auto const key = cacheKeyForInput(ctx, vids[index]);
    if (!key.has_value()) { continue; }
    updates.push_back(
      probecache::Entry{
        .key = key.value(),
        .chosenCq = plan.chosenCq,
        .p5 = plan.p5.value_or(0.0),
        .estimatedBytes = plan.estimatedBytes.value_or(0),
        .metric = metricToString(plan.metric),
        .unreachableFloor = plan.unreachableFloor,
      }
    );
  }
  if (!updates.empty()) { probecache::save(updates); }
}

// Re-encoding would not shrink the file when the estimate exceeds the source
// size; such plans are flagged and excluded from the encode stage.
auto probePlanRatio(ProbePlan const& plan) -> std::optional<double> {
  if (!plan.estimatedBytes.has_value()) { return std::nullopt; }
  auto ec = std::error_code{};
  auto const sourceBytes = fs::file_size(plan.inputPath, ec);
  if (sourceBytes == 0) { return std::nullopt; }
  return static_cast<double>(plan.estimatedBytes.value())
    / static_cast<double>(sourceBytes);
}

bool estimateExceedsSource(ProbePlan const& plan) {
  auto const ratio = probePlanRatio(plan);
  return ratio.has_value() && ratio.value() > 1.0;
}

auto collectProbeResults(
  std::span<fs::path const> vids,
  std::vector<ProbePlan> const& plans,
  taskexec::TaskRunResult const& runState,
  std::vector<std::size_t> const& taskVids
) -> ProbePhaseResult {
  auto result = ProbePhaseResult{};
  // A short/failed probe still runs a task and keeps its default-CQ plan;
  // cache hits carry probed==true with no task. Map the task-indexed
  // attempted flags back to vid indices (hits shrink the task list, hence
  // the explicit mapping) and keep only files that were measured or probed.
  auto measured = std::vector<char>(vids.size(), 0);
  for (auto taskIndex = std::size_t{0}; taskIndex < taskVids.size(); ++taskIndex) {
    if (runState.attempted.size() > taskIndex && runState.attempted[taskIndex] != 0) {
      measured[taskVids[taskIndex]] = 1;
    }
  }
  for (auto index = std::size_t{0}; index < vids.size(); ++index) {
    if (!plans[index].probed && measured[index] == 0) { continue; }
    auto plan = plans[index];
    plan.skipEncode = estimateExceedsSource(plan);
    result.plans[vids[index]] = plan;
    if (plan.unreachableFloor) {
      result.attentionWarnings.push_back(
        std::format(
          "{}: quality floor unreachable (p5 {:.2f} at CQ {}); encoding at CQ {}",
          vids[index].string(),
          plan.p5.value_or(0.0),
          plan.chosenCq,
          plan.chosenCq
        )
      );
    }
  }
  return result;
}

// Resolves cache hits up front: a hit fills plans[i] and is skipped; the
// remaining indices become probe tasks. Kept separate from task building so
// the slot bars can be sized to the actual task count.
void scanProbeCache(
  appctx::AppContext& ctx,
  std::span<fs::path const> vids,
  std::vector<probecache::Entry> const& cached,
  std::vector<ProbePlan>& plans,
  std::vector<std::size_t>& taskVids
) {
  taskVids.clear();
  taskVids.reserve(vids.size());

  for (auto index = std::size_t{0}; index < vids.size(); ++index) {
    if (auto const key = cacheKeyForInput(ctx, vids[index]); key.has_value()) {
      if (
        auto const it = std::ranges::find_if(
          cached,
          [&key](probecache::Entry const& e) { return e.key == key.value(); }
        );
        it != cached.end()
      ) {
        plans[index] = planFromCache(*it, vids[index]);
        LOG_DEBUG(
          "Probe cache hit: {} (CQ {})",
          vids[index].string(),
          plans[index].chosenCq
        );
        continue;
      }
    }
    taskVids.push_back(index);
  }
}

auto buildProbeTasks(
  appctx::AppContext& ctx,
  std::span<fs::path const> vids,
  std::span<std::size_t const> taskVids,
  fs::path const& probeRoot,
  ProbeProgress const& progress,
  std::vector<ProbePlan>& plans,
  std::size_t workerCount
) -> std::vector<taskexec::TaskSpec> {
  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(taskVids.size());
  for (auto const index: taskVids) {
    tasks.push_back(
      buildProbeTaskSpec(ctx, vids, index, probeRoot, progress, plans, workerCount)
    );
  }
  return tasks;
}

}  // namespace

// NOLINTNEXTLINE(readability-function-size): cache scan + slot bars + task run; phases delimit blocks
auto runProbePhase(appctx::AppContext& ctx, std::span<fs::path const> vids)
  -> eh::Result<ProbePhaseResult> {
  auto result = ProbePhaseResult{};
  if (vids.empty()) { return result; }

  auto const probeRoot =
    createProbeRoot();  // NOLINT(performance-no-automatic-move): read multiple times; const is intentional
  if (!probeRoot) { return eh::makeError("{}", probeRoot.error()); }
  videoworkflow::ProbeRootCleanupGuard
    rootGuard{probeRoot.value(), 6, std::chrono::milliseconds{500}, true};

  auto const cached = probecache::load();

  auto plans = std::vector<ProbePlan>(vids.size());
  auto const workerCount =
    std::max<std::size_t>(1, ctx.config.maxParallelJobs.value_or(4));
  auto progressCtx = progress::ProgressContext{};
  auto const overallBar = vids.size() > workerCount
    ? std::optional<std::size_t>{progressCtx.addBar(
        std::format("Probing: 0/{} files", vids.size()),
        progress::Tone::Overall
      )}
    : std::nullopt;
  // Slot bars mirror the encode bars: one per actual worker, reused across
  // tasks. Sized after the cache scan so fewer files than workers do not
  // leave idle bars on screen for the whole phase.
  auto taskVids = std::vector<std::size_t>{};
  scanProbeCache(ctx, vids, cached, plans, taskVids);
  auto const slotCount = taskexec::resolveWorkerCount(taskVids.size(), workerCount);
  auto const slotBars = initSlotBars(progressCtx, slotCount);
  auto slotProgress = std::vector<std::atomic<float>>(slotCount);
  auto completed = std::atomic_size_t{0};
  auto const updateOverall = [&] {
    if (!overallBar.has_value()) { return; }
    auto activeSum = 0.0f;
    for (auto const& slotProg: slotProgress) { activeSum += slotProg.load() / 100.0f; }
    auto const done = completed.load();
    progressCtx.setProgress(
      overallBar.value(),
      std::min(
        100.0f,
        (static_cast<float>(done) + activeSum) / static_cast<float>(vids.size()) * 100.0f
      )
    );
    progressCtx.setPostfixText(
      overallBar.value(),
      std::format("Probing: {}/{} files", done, vids.size())
    );
  };

  ProbeProgress const progress{
    .progressCtx = progressCtx,
    .slotBars = slotBars,
    .slotProgress = slotProgress,
    .completed = completed,
    .updateOverall = updateOverall,
  };

  auto tasks =
    buildProbeTasks(ctx, vids, taskVids, *probeRoot, progress, plans, workerCount);

  auto const runState = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = workerCount,
    .progress = &progressCtx,
    .hideCursor = true,
  });

  progressCtx.eraseBars();
  if (stopsignal::isStopRequested()) {
    LOG_INFO("Probing aborted by stop request.");
    return eh::makeError("Probing canceled by user.");
  }

  // The bars are gone; a single line replaces them.
  terminal::println(Info, "Probing complete: {} file(s).", vids.size());

  // Persist fresh decisions so the next run skips probing.
  flushCachePlans(ctx, vids, plans);

  return collectProbeResults(vids, plans, runState, taskVids);
}

auto formatProbeP5(ProbePlan const& plan) -> std::string {
  auto const p5 = plan.p5.value_or(0.0);
  if (plan.metric == videoquality::QualityMetric::Ssim) {
    return std::format("{:.3f}", p5);
  }
  if (plan.metric == videoquality::QualityMetric::Xpsnr) {
    // dB values can be negative; always render the explicit unit.
    return std::format("{:.2f} dB", p5);
  }
  return p5 < 95.0 ? std::format("{:.2f}", p5) : std::format("{:.1f}", p5);
}

auto padToDisplayWidth(std::string_view text, std::size_t width) -> std::string {
  auto const used = displaytext::displayWidth(text);
  if (used >= width) { return std::string{text}; }
  return std::string{text} + std::string(width - used, ' ');
}

struct PlanStats {
  std::vector<ProbePlan const*> normal;
  std::vector<ProbePlan const*> warnings;
  std::uintmax_t totalEst = 0;
  std::uintmax_t totalSource = 0;
  std::size_t estCount = 0;
};

auto collectPlanStats(std::span<ProbePlan const> plans) -> PlanStats {
  auto stats = PlanStats{};
  stats.normal.reserve(plans.size());
  for (auto const& plan: plans) {
    auto ec = std::error_code{};
    auto const sourceBytes = fs::file_size(plan.inputPath, ec);
    stats.totalSource += sourceBytes;
    if (plan.skipEncode) {
      // Skipped files are not encoded: on disk they keep their source size.
      stats.totalEst += sourceBytes;
      ++stats.estCount;
    } else if (plan.estimatedBytes.has_value()) {
      stats.totalEst += plan.estimatedBytes.value();
      ++stats.estCount;
    }
    (plan.unreachableFloor ? stats.warnings : stats.normal).push_back(&plan);
  }
  auto sortByName = [](std::vector<ProbePlan const*>& group) {
    std::ranges::sort(group, [](ProbePlan const* a, ProbePlan const* b) {
      return displaytext::pathToUtf8String(a->inputPath.filename())
        < displaytext::pathToUtf8String(b->inputPath.filename());
    });
  };
  sortByName(stats.normal);
  sortByName(stats.warnings);
  return stats;
}

constexpr auto kSkippedSuffix = std::string_view{" (skipped: est. > source)"};
constexpr auto kRuleFixedWidth =
  std::size_t{34};  // sum of the fixed numeric column widths

// The name column never needs to be wider than the longest file name in
// this batch; cap it so a very wide terminal does not pad short names
// across the screen.
auto resolvePlanNameWidth(
  std::span<ProbePlan const> plans,
  std::optional<displaytext::TableLayout> const& layout
) -> std::optional<std::size_t> {
  if (!layout.has_value()) { return std::nullopt; }
  auto maxName = std::size_t{0};
  for (auto const& plan: plans) {
    maxName = std::max(
      maxName,
      displaytext::displayWidth(displaytext::pathToUtf8String(plan.inputPath.filename()))
    );
  }
  return std::min(layout.value().nameWidth, std::max(maxName, std::size_t{20}));
}

// marker is the warning glyph ("\xE2\x9A\xA0 ") or empty; the two-space
// indent is fixed, so the name column always starts at the same offset
// and the numeric columns align with the header. Padding is applied by
// display width (not code points), so wide glyphs and CJK names align.
auto formatProbePlanRow(
  ProbePlan const& plan,
  std::string_view marker,
  std::size_t nameWidth
) -> std::string {
  auto const prefixWidth = [](std::string_view prefix) -> std::size_t {
    return displaytext::displayWidth(prefix);
  };
  auto const name = displaytext::truncateMiddle(
    displaytext::pathToUtf8String(plan.inputPath.filename()),
    nameWidth - prefixWidth(marker)
  );
  auto const ratio = probePlanRatio(plan);
  auto const nameCell = marker.empty()
    ? padToDisplayWidth(name, nameWidth)
    : std::string{marker} + padToDisplayWidth(name, nameWidth - prefixWidth(marker));
  if (!plan.probed) {
    return std::format(
      "  {}  {:>3}  {:>6}  {:>9}  {:<6}",
      nameCell,
      plan.chosenCq,
      "\xE2\x80\x94",
      "\xE2\x80\x94",
      "\xE2\x80\x94"
    );
  }
  auto suffix = std::string{};
  if (plan.skipEncode) { suffix += kSkippedSuffix; }
  if (plan.fromCache) { suffix += " (cached)"; }
  return std::format(
    "  {}  {:>3}  {:>6}  {:>9}  {:<6}{}",
    nameCell,
    plan.chosenCq,
    formatProbeP5(plan),
    displaytext::formatSizeBytes(plan.estimatedBytes),
    ratio.has_value() ? displaytext::formatSignedPercent(ratio.value()) : "\xE2\x80\x94",
    suffix
  );
}

// Very narrow terminal: name on its own line, metrics indented below.
void printTwoLineRow(ProbePlan const& plan, std::string_view marker) {
  auto const ratio = probePlanRatio(plan);
  if (!plan.probed) {
    terminal::println(
      Plain,
      "  {}{} (default; not probed)",
      marker,
      displaytext::pathToUtf8String(plan.inputPath.filename())
    );
    return;
  }
  terminal::println(
    Plain,
    "  {}{}",
    marker,
    displaytext::pathToUtf8String(plan.inputPath.filename())
  );
  if (plan.fromCache) { terminal::println(Plain, "    (cached decision)"); }
  terminal::println(
    Plain,
    "    CQ {} \xC2\xB7 p5 {} \xC2\xB7 {} \xC2\xB7 {}{}",
    plan.chosenCq,
    formatProbeP5(plan),
    displaytext::formatSizeBytes(plan.estimatedBytes),
    ratio.has_value() ? displaytext::formatSignedPercent(ratio.value()) : "\xE2\x80\x94",
    plan.skipEncode ? kSkippedSuffix : ""
  );
}

auto probeRule(std::size_t width) -> std::string {
  auto rule = std::string{};
  rule.reserve(width * 3);
  for (auto index = std::size_t{0}; index < width; ++index) { rule += "\xE2\x94\x80"; }
  return rule;
}

void printProbePlan(std::span<ProbePlan const> plans, int minVmafFloor) {
  auto const layout = displaytext::layoutColumns(consolewidth::resolveColumns());
  auto const nameWidth = resolvePlanNameWidth(plans, layout);
  // Rule matches the table width when the table renders; fixed width on the
  // two-line fallback.
  auto const ruleWidth = layout.has_value()
    ? nameWidth.value_or(std::size_t{40}) + kRuleFixedWidth
    : std::size_t{40};

  terminal::println(Plain, "{}", probeRule(ruleWidth));
  terminal::println(Plain, "Encoding plan (min p5-VMAF-equivalent {}):", minVmafFloor);

  auto const stats = collectPlanStats(plans);

  if (!stats.warnings.empty()) {
    terminal::println(
      Warning,
      "\xE2\x9A\xA0 {} file(s) can't reach the floor; encoded at the lowest CQ {}.",
      stats.warnings.size(),
      stats.warnings.front()->chosenCq
    );
  }

  if (!layout.has_value()) {
    for (auto const* plan: stats.normal) { printTwoLineRow(*plan, ""); }
    for (auto const* plan: stats.warnings) { printTwoLineRow(*plan, "\xE2\x9A\xA0 "); }
  } else {
    // layout has a value here, so resolvePlanNameWidth cannot be nullopt
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): same invariant as the layout branch
    auto const width = nameWidth.value();
    auto lines = std::vector<std::string>{};
    lines.reserve(plans.size() + 1);
    lines.push_back(
      std::format(
        "  {}  {:>3}  {:>6}  {:>9}  {:<6}",
        padToDisplayWidth("File", width),
        "CQ",
        "p5",
        "Est.Size",
        "Ratio"
      )
    );
    for (auto const* plan: stats.normal) {
      lines.push_back(formatProbePlanRow(*plan, "", width));
    }
    for (auto const* plan: stats.warnings) {
      lines.push_back(formatProbePlanRow(*plan, "\xE2\x9A\xA0 ", width));
    }
    terminal::write(
      terminal::Stream::Stdout,
      std::ranges::to<std::string>(lines | std::views::join_with('\n')),
      true
    );
  }

  auto const ratio = stats.totalSource > 0
    ? static_cast<double>(stats.totalEst) / static_cast<double>(stats.totalSource)
    : 0.0;
  terminal::println(
    Plain,
    "  Total: {} file(s), est. {}, source {} ({})",
    plans.size(),
    displaytext::formatSizeBytes(
      stats.estCount > 0 ? std::optional{stats.totalEst} : std::nullopt
    ),
    displaytext::formatSizeBytes(stats.totalSource),
    displaytext::formatSignedPercent(ratio)
  );
  terminal::println(Plain, "{}", probeRule(ruleWidth));
}

}  // namespace encodeprobe
