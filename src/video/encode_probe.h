#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"
#include "video/encode_config.h"
#include "video/video_quality.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace encodeprobe {

constexpr auto kProbeWindowDurationUs = std::uint64_t{10'000'000};
constexpr auto kProbeBudgetUs = std::uint64_t{40'000'000};  // two non-overlapping windows
constexpr auto kDefaultCq = 28;
constexpr auto kMinCq = 16;
constexpr auto kMaxCq = 40;
constexpr auto kCqStep = 4;
constexpr auto kBaseCqs = std::array{24, 28, 32};

struct ProbeWindow {
  std::uint64_t startUs;
  std::uint64_t durationUs;
};

// Two uniform 10s windows at 25%/75% of the duration; nullopt when the video
// is shorter than the probe budget (no two non-overlapping windows).
auto pickProbeWindows(std::uint64_t totalDurationUs)
  -> std::optional<std::pair<ProbeWindow, ProbeWindow>>;

struct ProbePoint {
  int cq;
  double p5;  // in the metric's own units (VMAF 0-100 or SSIM 0-1)
  videoquality::QualityMetric metric;
  std::uint64_t segmentBytes;
};

struct ProbeDecision {
  int cq;
  double p5;               // measured/interpolated p5 at the chosen cq
  double videoBitrateBps;  // interpolated video bitrate of the chosen cq
  bool unreachableFloor;   // no probed cq met the floor
};

// Highest CQ whose p5 meets the floor, interpolating linearly between the
// adjacent probed points (rounded down). When no probed CQ meets the floor,
// degrades to the lowest probed CQ with unreachableFloor set. The floor is a
// VMAF floor; SSIM points are compared against ssimFloorForVmafFloor(floor).
auto decideCq(std::span<ProbePoint const> points, int vmafFloor) -> ProbeDecision;

using ProbeMeasure = std::function<std::optional<ProbePoint>(int cq)>;

// Progress callback: (completed point count, cq just measured). Optional;
// unit tests and pure callers omit it. kMaxProbePoints = 5 (3 base + 2
// extension) bounds the progress fraction.
constexpr auto kMaxProbePoints = std::size_t{5};
using ProbePointCallback = std::function<void(std::size_t completed, int cq)>;

// Sub-step callback fired at the start of each encode/score step inside a
// point: (cq, phase) where phase is like "encode 1/2" or "score 2/2".
// kMaxProbeSteps bounds the progress fraction.
constexpr auto kStepsPerProbePoint = std::size_t{4};
constexpr auto kMaxProbeSteps = kMaxProbePoints * kStepsPerProbePoint;
using ProbeStepCallback = std::function<void(int cq, std::string_view phase)>;

// Measures base {24,28,32}, then extends by kCqStep bounded to [kMinCq,kMaxCq]
// until the floor is bracketed: probes 20/16 while the floor is unmet, 36/40
// while it is met. Nullopt when any measurement fails (caller falls back to
// the default CQ).
auto probeCqSequence(
  ProbeMeasure const& measure,
  int vmafFloor,
  ProbePointCallback onPoint = {}
) -> std::optional<std::vector<ProbePoint>>;

// Probe config: the production segment config (shared construction path) with
// only CQ and the output path differing — the invariant tests assert this.
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
) -> EncodeConfig;

struct ProbePlan {
  fs::path inputPath;
  int chosenCq = kDefaultCq;
  videoquality::QualityMetric metric = videoquality::QualityMetric::Vmaf;
  std::optional<double> p5;
  std::optional<std::uintmax_t> estimatedBytes;
  bool probed = false;  // false: skipped (short video / scoring failed)
  bool unreachableFloor = false;
};

struct ProbePhaseResult {
  appctx::path_map<ProbePlan> plans;
  std::vector<std::string> attentionWarnings;
};

// Probes every video in parallel (MP4 only, called with --crf absent), probe
// artifacts in a per-run temp dir cleaned up after use. Per-file measurement
// failures degrade to the default CQ; a stop request aborts the whole phase.
// Shows one progress bar per file (plus an overall bar for many files).
auto runProbePhase(appctx::AppContext& ctx, std::span<fs::path const> vids)
  -> eh::Result<ProbePhaseResult>;

auto printProbePlan(std::span<ProbePlan const> plans, int minVmafFloor) -> void;

// One-line summary hint pointing at the comparison tool.
inline auto previewHint(fs::path const& original, fs::path const& encoded)
  -> std::string {
  return std::format("encro preview \"{}\" \"{}\"", original.string(), encoded.string());
}

}  // namespace encodeprobe
