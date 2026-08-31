#include "video/video_quality.h"

#include "core/work_dirs.h"
#include "utils/utils.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

using namespace std::literals;

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::VIDEO_QUALITY);

namespace fs = std::filesystem;

namespace videoquality {

namespace {

constexpr auto kSsimFloorAnchors = std::array{
  std::pair{90, 0.970},
  std::pair{95, 0.980},
  std::pair{97, 0.985},
};

// XPSNR thresholds (dB) calibrated against VMAF on a 5-sample corpus
// (2026-08: two noisy live-action, one anime, one 60fps, one low-res
// low-bitrate), production chain: nvenc hevc p5, two pooled 10s windows,
// p5 rule. Cross-content stdev ~1.4dB (~±3 VMAF points); anime needs
// ~+2.7dB more at the same floor — recalibrate when that matters.
constexpr auto kXpsnrFloorAnchors = std::array{
  std::pair{90, 38.5},
  std::pair{95, 41.0},
  std::pair{97, 42.5},
};

constexpr auto kHdrTransfers = std::array{
  "smpte2084"sv,
  "arib-std-b67"sv,
  "smpte428"sv,
};

double seconds(std::uint64_t micros) {
  return static_cast<double>(micros) / 1'000'000.0;
}

// Filter option values are tokenized twice (graph parse, then the filter's
// own option parsing): a ':' must be quoted AND backslash-escaped to survive
// both layers, and backslashes are consumed as escapes, so paths use forward
// slashes with only the drive colon escaped (e.g. 'C\:/Users/...').
auto quoteFilterPath(fs::path const& path) -> std::string {
  auto generic = path.generic_string();
  auto escaped = std::string{};
  escaped.reserve(generic.size() + 2);
  for (auto const ch: generic) {
    if (ch == ':') {
      escaped += "\\:";
    } else {
      escaped += ch;
    }
  }
  return std::format("'{}'", escaped);
}

auto parseVmafFrameScores(boost::json::value const& log) -> std::vector<double> {
  auto scores = std::vector<double>{};
  if (!log.is_object()) { return scores; }

  auto const framesIt = log.as_object().find("frames");
  if (framesIt == log.as_object().end() || !framesIt->value().is_array()) {
    return scores;
  }

  for (auto const& frame: framesIt->value().as_array()) {
    if (!frame.is_object()) { continue; }
    auto const metricsIt = frame.as_object().find("metrics");
    if (metricsIt == frame.as_object().end() || !metricsIt->value().is_object()) {
      continue;
    }
    auto const vmafIt = metricsIt->value().as_object().find("vmaf");
    if (vmafIt == metricsIt->value().as_object().end()) { continue; }
    if (!vmafIt->value().is_double() && !vmafIt->value().is_int64()) { continue; }
    scores.push_back(
      vmafIt->value().is_double() ? vmafIt->value().as_double()
                                  : static_cast<double>(vmafIt->value().as_int64())
    );
  }

  return scores;
}

auto runScoringCommand(std::string const& cmd) -> eh::Result<void> {
  ExecResult result{};
  try {
    result = exec2(cmd, false);
  } catch (std::exception const& ex) {
    LOG_WARN("Scoring command could not be launched: {}", ex.what());
    return eh::makeError("Scoring command could not be launched: {}", ex.what());
  }
  if (result.pid.has_value()) {
    LOG_DEBUG("Scoring command pid: {}", result.pid.value());
  }
  if (result.exitCode != 0) {
    LOG_DEBUG(
      "Scoring command failed: exitCode={} output-bytes={}",
      result.exitCode,
      result.output.size()
    );
    return eh::makeError("Scoring command exited with code {}.", result.exitCode);
  }
  return {};
}

// Both inputs are seeked to the window start (-ss before each -i), after
// which PTS restarts near zero (offset by the GOP boundary the seek landed
// on), so both sides trim at local timestamps.
auto buildTrimmedPairChain(std::uint64_t durationUs) -> std::string {
  auto const durSec = seconds(durationUs);
  return std::format(
    "[0:v]trim=start=0.000000:end={:.6f},setpts=PTS-STARTPTS[v0];"
    "[1:v]trim=start=0.000000:end={:.6f},setpts=PTS-STARTPTS[v1];"
    "[v1][v0]scale2ref=w=main_w:h=main_h[enc][ref]",
    durSec,
    durSec
  );
}

// Frame lines look like "n:    1  XPSNR y: 47.9710  XPSNR u: 51.4535 ...";
// the filter's summary line does not start with "n:" and is skipped. Each
// frame's score is the mean of its plane dB values.
auto parseXpsnrFrameScores(std::string_view content) -> std::vector<double> {
  auto scores = std::vector<double>{};
  for (auto const line: std::views::split(content, '\n')) {
    auto const text = std::string_view{line};
    auto const trimmedStart = text.find_first_not_of(" \t\r");
    if (
      trimmedStart == std::string_view::npos || text.compare(trimmedStart, 2, "n:") != 0
    ) {
      continue;
    }

    auto sum = 0.0;
    auto planes = 0;
    auto pos = text.find("XPSNR", trimmedStart);
    while (pos != std::string_view::npos) {
      auto const colon = text.find(':', pos + sizeof("XPSNR") - 1);
      if (colon == std::string_view::npos) { break; }
      // stod skips leading whitespace and stops at the first non-numeric
      // char, so "47.9710  XPSNR..." parses cleanly.
      try {
        sum += std::stod(std::string{text.substr(colon + 1)});
        ++planes;
      } catch (...) {  // NOLINT(bugprone-empty-catch): format probe; try next marker
      }
      pos = text.find("XPSNR", colon + 1);
    }
    if (planes > 0) { scores.push_back(sum / static_cast<double>(planes)); }
  }
  return scores;
}

// Shared body for the three scoring filters: identical input/seek/trim
// plumbing, differing only in the filter clause, link label, and parser.
auto runScoringFilter(
  QualityRequest const& request,
  fs::path const& artifactPath,
  std::string_view filterClause,
  std::string_view linkLabel,
  std::function<eh::Result<std::vector<double>>(fs::path const&)> const& parse
) -> eh::Result<std::vector<double>> {
  auto const chain = buildTrimmedPairChain(request.durationUs);
  auto const startSec = seconds(request.startUs);
  auto const encodedInput = !request.encodedHasLocalPts
    ? std::format("-ss {:.6f} -i \"{}\"", startSec, request.encodedPath.string())
    : std::format("-i \"{}\"", request.encodedPath.string());
  auto const cmd = std::format(
    "{} -hide_banner -nostats -loglevel error -y -ss {:.6f} -i \"{}\" {} "
    "-filter_complex \"{};[enc][ref]{}={}[{}]\" "
    "-map \"[{}]\" -f null -",
    quoteToolPath(request.ffmpegPath),
    startSec,
    request.originalPath.string(),
    encodedInput,
    chain,
    filterClause,
    quoteFilterPath(artifactPath),
    linkLabel,
    linkLabel
  );

  auto const runRes = runScoringCommand(cmd);
  if (!runRes) { return eh::makeError("{}", runRes.error()); }

  return parse(artifactPath);
}

auto runVmaf(QualityRequest const& request, fs::path const& logPath)
  -> eh::Result<std::vector<double>> {
  return runScoringFilter(
    request,
    logPath,
    "libvmaf=log_fmt=json:log_path",
    "vnul",
    parseVmafLog
  );
}

auto runSsim(QualityRequest const& request, fs::path const& statsPath)
  -> eh::Result<std::vector<double>> {
  return runScoringFilter(request, statsPath, "ssim=stats_file", "snul", parseSsimStats);
}

auto runXpsnr(QualityRequest const& request, fs::path const& statsPath)
  -> eh::Result<std::vector<double>> {
  return runScoringFilter(
    request,
    statsPath,
    "xpsnr=stats_file",
    "xnul",
    parseXpsnrStats
  );
}

double floorFromAnchors(std::span<std::pair<int, double> const> anchors, int vmafFloor) {
  auto const anchor = std::ranges::find_if(anchors, [vmafFloor](auto const& a) {
    return a.first == vmafFloor;
  });
  if (anchor != anchors.end()) { return anchor->second; }

  if (vmafFloor <= anchors.front().first) { return anchors.front().second; }
  if (vmafFloor >= anchors.back().first) { return anchors.back().second; }

  auto const next = std::ranges::find_if(anchors, [vmafFloor](auto const& a) {
    return a.first > vmafFloor;
  });
  auto const& low = *(next - 1);
  auto const& high = *next;
  auto const t = static_cast<double>(vmafFloor - low.first)
    / static_cast<double>(high.first - low.first);
  return low.second + t * (high.second - low.second);
}

}  // namespace

auto percentile(std::span<double const> scores, double percentile)
  -> std::optional<double> {
  if (scores.empty()) { return std::nullopt; }

  auto sorted = std::vector<double>{scores.begin(), scores.end()};
  std::ranges::sort(sorted);

  auto const rank = std::clamp(
    static_cast<std::size_t>(std::ceil(
      // NOLINTNEXTLINE(bugprone-narrowing-conversions): percentile math needs double
      percentile / 100.0 * sorted.size()
    )),
    std::size_t{1},
    sorted.size()
  );
  return sorted[rank - 1];
}

auto metricName(QualityMetric metric) -> std::string_view {
  using enum QualityMetric;
  switch (metric) {
    case Ssim : return "SSIM";
    case Xpsnr: return "XPSNR";
    case Vmaf : break;
  }
  return "VMAF";
}

double ssimFloorForVmafFloor(int vmafFloor) {
  return floorFromAnchors(kSsimFloorAnchors, vmafFloor);
}

double xpsnrFloorForVmafFloor(int vmafFloor) {
  return floorFromAnchors(kXpsnrFloorAnchors, vmafFloor);
}

bool isHdrVideo(boost::json::value const& vidInfo) {
  if (!vidInfo.is_object()) { return false; }

  auto const streamsIt = vidInfo.as_object().find("streams");
  if (streamsIt == vidInfo.as_object().end() || !streamsIt->value().is_array()) {
    return false;
  }

  for (auto const& streamVal: streamsIt->value().as_array()) {
    if (!streamVal.is_object()) { continue; }
    auto const& stream = streamVal.as_object();
    auto const codecTypeIt = stream.find("codec_type");
    if (
      codecTypeIt == stream.end()
      || !codecTypeIt->value().is_string()
      || codecTypeIt->value().as_string() != "video"
    ) {
      continue;
    }

    if (
      auto const depthIt = stream.find("bits_per_raw_sample"); depthIt != stream.end()
      && depthIt->value().is_int64()
      && depthIt->value().as_int64() > 8
    ) {
      return true;
    }
    if (
      auto const depthIt = stream.find("bits_per_sample"); depthIt != stream.end()
      && depthIt->value().is_int64()
      && depthIt->value().as_int64() > 8
    ) {
      return true;
    }
    if (
      auto const transferIt = stream.find("color_transfer");
      transferIt != stream.end() && transferIt->value().is_string()
    ) {
      auto const transfer = std::string_view{transferIt->value().as_string()};
      if (std::ranges::contains(kHdrTransfers, transfer)) { return true; }
    }
  }

  return false;
}

auto parseVmafLog(fs::path const& logPath) -> eh::Result<std::vector<double>> {
  auto input = std::ifstream{logPath, std::ios::binary};
  if (!input.is_open()) {
    return eh::makeError("VMAF log not found: {}", logPath.string());
  }

  auto content = std::string{std::istreambuf_iterator<char>{input}, {}};
  auto log = boost::json::value{};
  try {
    log = boost::json::parse(content);
  } catch (std::exception const& ex) {
    return eh::makeError("Failed to parse VMAF log {}: {}", logPath.string(), ex.what());
  }

  auto scores = parseVmafFrameScores(log);
  if (scores.empty()) {
    return eh::makeError("VMAF log contains no frame scores: {}", logPath.string());
  }
  return scores;
}

auto parseSsimStats(fs::path const& statsPath) -> eh::Result<std::vector<double>> {
  auto input = std::ifstream{statsPath, std::ios::binary};
  if (!input.is_open()) {
    return eh::makeError("SSIM stats file not found: {}", statsPath.string());
  }

  auto scores = std::vector<double>{};
  auto line = std::string{};
  while (std::getline(input, line)) {
    // Modern ffmpeg prints "n:1 Y:0.868 U:0.738 V:0.761 All:0.829 (dB)";
    // legacy builds printed "... ssim:0.984500".
    auto score = std::optional<double>{};
    for (auto const marker: {"ssim:", "All:"}) {
      auto const pos = line.rfind(marker);
      if (pos == std::string::npos) { continue; }
      try {
        score = std::stod(line.substr(pos + std::char_traits<char>::length(marker)));
        break;
      } catch (...) { /* NOLINT(bugprone-empty-catch): format probe; try next marker */
      }
    }
    if (score.has_value()) { scores.push_back(score.value()); }
  }

  if (scores.empty()) {
    return eh::makeError("SSIM stats file contains no scores: {}", statsPath.string());
  }
  return scores;
}

auto parseXpsnrStats(fs::path const& statsPath) -> eh::Result<std::vector<double>> {
  auto input = std::ifstream{statsPath, std::ios::binary};
  if (!input.is_open()) {
    return eh::makeError("XPSNR stats file not found: {}", statsPath.string());
  }

  auto const content = std::string{std::istreambuf_iterator<char>{input}, {}};
  auto scores = parseXpsnrFrameScores(content);
  if (scores.empty()) {
    return eh::makeError(
      "XPSNR stats file contains no frame scores: {}",
      statsPath.string()
    );
  }
  return scores;
}

// encodedHasLocalPts: probe segments carry segment-local PTS (their -ss seek
// already happened), so only the original is seeked to the window. Full-file
// inputs (preview) are seeked on both sides.
auto measureSegmentQuality(QualityRequest const& request) -> eh::Result<SegmentScores> {
  if (request.durationUs == 0) {
    return eh::makeError("Segment duration must be non-zero.");
  }

  workdirs::ensureScratchDir();
  auto const logDir = workdirs::scratchDir();
  auto const vmafLog = logDir / std::format("vmaf_{}.json", getUUID());
  auto const ssimStats = logDir / std::format("ssim_{}.txt", getUUID());
  auto const xpsnrStats = logDir / std::format("xpsnr_{}.txt", getUUID());
  auto const removeLogs = [&] {
    auto ec = std::error_code{};
    fs::remove(vmafLog, ec);
    fs::remove(ssimStats, ec);
    fs::remove(xpsnrStats, ec);
  };

  if (!isHdrVideo(request.originalVideoInfo)) {
    auto vmafRes = runVmaf(request, vmafLog);
    if (vmafRes.has_value()) {
      removeLogs();
      return SegmentScores{QualityMetric::Vmaf, std::move(*vmafRes)};
    }
    LOG_WARN(
      "VMAF unavailable for segment (original={} start={}us); falling back to XPSNR: {}",
      request.originalPath.string(),
      request.startUs,
      vmafRes.error()
    );

    auto xpsnrRes = runXpsnr(request, xpsnrStats);
    if (xpsnrRes.has_value()) {
      removeLogs();
      return SegmentScores{QualityMetric::Xpsnr, std::move(*xpsnrRes)};
    }
    LOG_WARN(
      "XPSNR unavailable for segment (original={} start={}us); falling back to SSIM: {}",
      request.originalPath.string(),
      request.startUs,
      xpsnrRes.error()
    );
  }

  auto ssimRes = runSsim(request, ssimStats);
  removeLogs();
  if (!ssimRes) {
    return eh::makeError(
      "Quality scores unparsable for segment of {}: {}",
      request.originalPath.string(),
      ssimRes.error()
    );
  }
  return SegmentScores{QualityMetric::Ssim, std::move(*ssimRes)};
}

}  // namespace videoquality
