#include "video/encode_probe.h"

#include "core/collision_naming.h"
#include "core/display_text.h"
#include "core/task_executor.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "utils/utils.h"
#include "video/video_info.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <utility>
#include <vector>

DEFINE_LOGGER(logtags::VIDEO_PROBE);

namespace fs = std::filesystem;
using enum terminal::MessageKind;

namespace encodeprobe {

namespace {

auto meetsFloor(ProbePoint const& point, int vmafFloor) -> bool {
  auto const floor = point.metric == videoquality::QualityMetric::Vmaf
    ? static_cast<double>(vmafFloor)
    : videoquality::ssimFloorForVmafFloor(vmafFloor);
  return point.p5 >= floor;
}

auto floorForMetric(videoquality::QualityMetric metric, int vmafFloor) -> double {
  return metric == videoquality::QualityMetric::Vmaf
    ? static_cast<double>(vmafFloor)
    : videoquality::ssimFloorForVmafFloor(vmafFloor);
}

auto runProbeEncode(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  EncodeInputSettings const& settings,
  fs::path const& segFile,
  int cq,
  ProbeWindow const& window
) -> bool {
  auto const cfg = buildProbeSegmentConfig(
    ctx.toolchain,
    inputPath,
    ctx.config.outputFormat,
    ctx.config.videoCodec,
    settings,
    cq,
    window.startUs,
    window.durationUs,
    segFile
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

auto measurePoint(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  EncodeInputSettings const& settings,
  fs::path const& probeDir,
  int cq,
  std::pair<ProbeWindow, ProbeWindow> const& windows,
  boost::json::value const& vidInfo
) -> std::optional<ProbePoint> {
  // Segments are per-video: parallel probes of different inputs share
  // probeDir, so the file name carries a per-input key.
  auto const vidKey = fs::hash_value(inputPath);
  auto const segA = probeDir / std::format("cq{}_{}_{}.ts", vidKey, cq, 0);
  auto const segB = probeDir / std::format("cq{}_{}_{}.ts", vidKey, cq, 1);
  if (!runProbeEncode(ctx, inputPath, settings, segA, cq, windows.first)) {
    return std::nullopt;
  }
  if (!runProbeEncode(ctx, inputPath, settings, segB, cq, windows.second)) {
    return std::nullopt;
  }

  auto const ffmpeg = ctx.toolchain.ffmpegPath.value_or(fs::path{"ffmpeg"});
  auto const scoresA = videoquality::measureSegmentQuality(
    ffmpeg,
    inputPath,
    segA,
    windows.first.startUs,
    windows.first.durationUs,
    vidInfo
  );
  auto const scoresB = videoquality::measureSegmentQuality(
    ffmpeg,
    inputPath,
    segB,
    windows.second.startUs,
    windows.second.durationUs,
    vidInfo
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

  auto allFrames = scoresA->frameScores;
  allFrames
    .insert(allFrames.end(), scoresB->frameScores.begin(), scoresB->frameScores.end());
  auto const p5 = videoquality::percentile(allFrames, 5.0);
  if (!p5.has_value()) { return std::nullopt; }

  auto ec = std::error_code{};
  auto const bytes = fs::file_size(segA, ec) + fs::file_size(segB, ec);
  return ProbePoint{cq, p5.value(), scoresA->metric, bytes};
}

auto audioBitrateBps(boost::json::value const& vidInfo) -> double {
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

auto probeSingleFile(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  fs::path const& probeDir
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

  auto const vidInfo = ctx.runtime.videoInfoCache.find(inputPath);
  auto const info = vidInfo.value_or(boost::json::value{});
  auto const settings = resolveInputEncodeSettings(
    ctx.toolchain,
    ctx.runtime,
    inputPath,
    ctx.config.nvencPreset
  );

  auto const points = probeCqSequence(
    [&](int cq) -> std::optional<ProbePoint> {
      return measurePoint(ctx, inputPath, settings, probeDir, cq, windows.value(), info);
    },
    ctx.config.minVmaf
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

auto formatSizeMB(std::optional<std::uintmax_t> bytes) -> std::string {
  if (!bytes.has_value()) { return "-"; }
  return std::format("{:.1f} MB", static_cast<double>(bytes.value()) / 1'048'576.0);
}

auto metricLabel(videoquality::QualityMetric metric) -> std::string_view {
  return metric == videoquality::QualityMetric::Vmaf ? "VMAF" : "SSIM";
}

}  // namespace

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

auto probeCqSequence(ProbeMeasure const& measure, int vmafFloor)
  -> std::optional<std::vector<ProbePoint>> {
  auto points = std::vector<ProbePoint>{};
  for (auto const cq: kBaseCqs) {
    auto const point = measure(cq);
    if (!point.has_value()) { return std::nullopt; }
    points.push_back(std::move(point.value()));
  }

  if (!meetsFloor(points.front(), vmafFloor)) {
    // Floor unmet at 24: step down until it is met or the floor is proven
    // unreachable (p5@16 still below).
    if (auto const point = measure(kMinCq + kCqStep); point.has_value()) {
      points.push_back(std::move(point.value()));
      if (!meetsFloor(points.back(), vmafFloor)) {
        if (auto const low = measure(kMinCq); low.has_value()) {
          points.push_back(std::move(low.value()));
        } else {
          return std::nullopt;
        }
      }
    } else {
      return std::nullopt;
    }
  } else if (meetsFloor(points.back(), vmafFloor)) {
    // Floor still met at 32: step up until it is missed or 40 is reached.
    if (auto const point = measure(kMaxCq - kCqStep); point.has_value()) {
      points.push_back(std::move(point.value()));
      if (meetsFloor(points.back(), vmafFloor)) {
        if (auto const high = measure(kMaxCq); high.has_value()) {
          points.push_back(std::move(high.value()));
        } else {
          return std::nullopt;
        }
      }
    } else {
      return std::nullopt;
    }
  }

  return points;
}

auto buildProbeSegmentConfig(
  appctx::ToolchainPaths const& toolchain,
  fs::path const& inputPath,
  std::string const& outputFormat,
  std::optional<std::string> const& videoCodec,
  EncodeInputSettings const& settings,
  int cq,
  std::uint64_t startUs,
  std::uint64_t durationUs,
  fs::path const& segFile
) -> EncodeConfig {
  return buildSegmentEncodeConfig(
    toolchain,
    inputPath,
    outputFormat,
    cq,
    videoCodec,
    settings,
    0,
    startUs,
    durationUs,
    segFile
  );
}

auto runProbePhase(appctx::AppContext& ctx, std::span<fs::path const> vids)
  -> eh::Result<ProbePhaseResult> {
  auto result = ProbePhaseResult{};
  if (vids.empty()) { return result; }

  auto const probeRoot =
    fs::temp_directory_path() / std::format("encro_probe_{}", getUUID());
  {
    auto ec = std::error_code{};
    fs::create_directories(probeRoot, ec);
    if (ec) {
      return eh::makeError(
        "Failed to create probe directory: {} ({})",
        probeRoot.string(),
        ec.message()
      );
    }
  }
  struct ProbeRootGuard {
    fs::path root;
    ~ProbeRootGuard() {
      auto ec = std::error_code{};
      fs::remove_all(root, ec);
    }
  } rootGuard{probeRoot};

  auto plans = std::vector<ProbePlan>(vids.size());
  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(vids.size());
  for (auto index = std::size_t{0}; index < vids.size(); ++index) {
    tasks.push_back({
      .id = std::format("probe:{}", collisionnaming::stablePathString(vids[index])),
      .label = vids[index].filename().string(),
      .input = vids[index].string(),
      .run = [&, index](taskexec::TaskContext&) -> eh::Result<void> {
        plans[index] = probeSingleFile(ctx, vids[index], probeRoot);
        return {};
      },
    });
  }

  auto const runState = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = std::max<std::size_t>(1, ctx.config.maxParallelJobs.value_or(4)),
    .progress = nullptr,
    .hideCursor = false,
  });

  if (stopsignal::isStopRequested()) {
    LOG_INFO("Probing aborted by stop request.");
    return eh::makeError("Probing canceled by user.");
  }

  for (auto index = std::size_t{0}; index < vids.size(); ++index) {
    if (runState.attempted[index] == 0) { continue; }
    auto const& plan = plans[index];
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

auto printProbePlan(std::span<ProbePlan const> plans, int minVmafFloor) -> void {
  terminal::println(
    Info,
    "Encoding plan (min p5-{} {}):",
    metricLabel(plans.empty() ? videoquality::QualityMetric::Vmaf : plans.front().metric),
    minVmafFloor
  );
  auto totalEst = std::uintmax_t{0};
  auto totalSource = std::uintmax_t{0};
  auto estCount = std::size_t{0};
  for (auto const& plan: plans) {
    auto ec = std::error_code{};
    totalSource += fs::file_size(plan.inputPath, ec);
    if (plan.estimatedBytes.has_value()) {
      totalEst += plan.estimatedBytes.value();
      ++estCount;
    }

    if (plan.unreachableFloor) {
      terminal::println(
        Warning,
        "  {}: CQ {} (p5 {} {:.2f}; floor unreachable)",
        displaytext::pathToUtf8String(plan.inputPath.filename()),
        plan.chosenCq,
        metricLabel(plan.metric),
        plan.p5.value_or(0.0)
      );
    } else if (!plan.probed) {
      terminal::println(
        Info,
        "  {}: CQ {} (default; not probed)",
        displaytext::pathToUtf8String(plan.inputPath.filename()),
        plan.chosenCq
      );
    } else {
      terminal::println(
        Info,
        "  {}: CQ {} (p5 {} {:.2f})",
        displaytext::pathToUtf8String(plan.inputPath.filename()),
        plan.chosenCq,
        metricLabel(plan.metric),
        plan.p5.value_or(0.0)
      );
    }
    terminal::println(Info, "    est. size: {}", formatSizeMB(plan.estimatedBytes));
    if (plan.estimatedBytes.has_value()) {
      auto const sourceBytes = fs::file_size(plan.inputPath, ec);
      auto const ratio = sourceBytes > 0
        ? static_cast<double>(plan.estimatedBytes.value())
          / static_cast<double>(sourceBytes)
        : 0.0;
      terminal::println(Info, "    ratio: {:.2f}", ratio);
    }
  }

  auto const ratio = totalSource > 0
    ? static_cast<double>(totalEst) / static_cast<double>(totalSource)
    : 0.0;
  terminal::println(
    Info,
    "  Total: {} file(s), est. {}, source {} (ratio {:.2f})",
    plans.size(),
    formatSizeMB(estCount > 0 ? std::optional{totalEst} : std::nullopt),
    formatSizeMB(totalSource),
    ratio
  );
}

}  // namespace encodeprobe
