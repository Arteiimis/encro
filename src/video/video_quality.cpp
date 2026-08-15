#include "video/video_quality.h"

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
#include <string>
#include <string_view>

using namespace std::literals;

DEFINE_LOGGER(logtags::VIDEO_QUALITY);

namespace fs = std::filesystem;

namespace videoquality {

namespace {

constexpr auto kSsimFloorAnchors = std::array{
  std::pair{90, 0.970},
  std::pair{95, 0.980},
  std::pair{97, 0.985},
};

constexpr auto kHdrTransfers = std::array{
  "smpte2084"sv,
  "arib-std-b67"sv,
  "smpte428"sv,
};

auto seconds(std::uint64_t micros) -> double {
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

auto isVmafLogEmpty(boost::json::value const& log) -> bool {
  if (!log.is_object()) { return true; }
  auto const framesIt = log.as_object().find("frames");
  if (framesIt == log.as_object().end() || !framesIt->value().is_array()) { return true; }
  return framesIt->value().as_array().empty();
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

auto runScoringCommand(fs::path const& ffmpegPath, std::string const& cmd)
  -> eh::Result<void> {
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

auto buildTrimmedPairChain(std::uint64_t startUs, std::uint64_t durationUs)
  -> std::string {
  // [0:v] is the original source: trim at source timestamps. [1:v] is the
  // probe segment: its PTS restarts near the GOP boundary the -ss seek landed
  // on (not at the source window), so trim at segment-local timestamps.
  auto const startSec = seconds(startUs);
  auto const endSec = seconds(startUs + durationUs);
  auto const durSec = seconds(durationUs);
  return std::format(
    "[0:v]trim=start={:.6f}:end={:.6f},setpts=PTS-STARTPTS[v0];"
    "[1:v]trim=start=0.000000:end={:.6f},setpts=PTS-STARTPTS[v1];"
    "[v1][v0]scale2ref=w=main_w:h=main_h[enc][ref]",
    startSec,
    endSec,
    durSec
  );
}

auto runVmaf(
  fs::path const& ffmpegPath,
  fs::path const& originalPath,
  fs::path const& encodedPath,
  std::uint64_t startUs,
  std::uint64_t durationUs,
  fs::path const& logPath
) -> eh::Result<std::vector<double>> {
  auto const chain = buildTrimmedPairChain(startUs, durationUs);
  auto const cmd = std::format(
    "{} -hide_banner -nostats -loglevel error -y -i \"{}\" -i \"{}\" "
    "-filter_complex \"{};[enc][ref]libvmaf=log_fmt=json:log_path={}[vnul]\" "
    "-map \"[vnul]\" -f null -",
    quoteToolPath(ffmpegPath),
    originalPath.string(),
    encodedPath.string(),
    chain,
    quoteFilterPath(logPath)
  );

  auto const runRes = runScoringCommand(ffmpegPath, cmd);
  if (!runRes) { return eh::makeError("{}", runRes.error()); }

  return parseVmafLog(logPath);
}

auto runSsim(
  fs::path const& ffmpegPath,
  fs::path const& originalPath,
  fs::path const& encodedPath,
  std::uint64_t startUs,
  std::uint64_t durationUs,
  fs::path const& statsPath
) -> eh::Result<std::vector<double>> {
  auto const chain = buildTrimmedPairChain(startUs, durationUs);
  auto const cmd = std::format(
    "{} -hide_banner -nostats -loglevel error -y -i \"{}\" -i \"{}\" "
    "-filter_complex \"{};[enc][ref]ssim=stats_file={}[snul]\" "
    "-map \"[snul]\" -f null -",
    quoteToolPath(ffmpegPath),
    originalPath.string(),
    encodedPath.string(),
    chain,
    quoteFilterPath(statsPath)
  );

  auto const runRes = runScoringCommand(ffmpegPath, cmd);
  if (!runRes) { return eh::makeError("{}", runRes.error()); }

  return parseSsimStats(statsPath);
}

}  // namespace

auto percentile(std::span<double const> scores, double percentile)
  -> std::optional<double> {
  if (scores.empty()) { return std::nullopt; }

  auto sorted = std::vector<double>{scores.begin(), scores.end()};
  std::ranges::sort(sorted);

  auto const rank = std::clamp(
    static_cast<std::size_t>(std::ceil(percentile / 100.0 * sorted.size())),
    std::size_t{1},
    sorted.size()
  );
  return sorted[rank - 1];
}

auto mean(std::span<double const> scores) -> std::optional<double> {
  if (scores.empty()) { return std::nullopt; }
  auto sum = 0.0;
  for (auto const score: scores) { sum += score; }
  return sum / static_cast<double>(scores.size());
}

auto ssimFloorForVmafFloor(int vmafFloor) -> double {
  auto const anchor = std::ranges::find_if(kSsimFloorAnchors, [vmafFloor](auto const& a) {
    return a.first == vmafFloor;
  });
  if (anchor != kSsimFloorAnchors.end()) { return anchor->second; }

  if (vmafFloor <= kSsimFloorAnchors.front().first) {
    return kSsimFloorAnchors.front().second;
  }
  if (vmafFloor >= kSsimFloorAnchors.back().first) {
    return kSsimFloorAnchors.back().second;
  }

  auto const next = std::ranges::find_if(kSsimFloorAnchors, [vmafFloor](auto const& a) {
    return a.first > vmafFloor;
  });
  auto const& low = *(next - 1);
  auto const& high = *next;
  auto const t = static_cast<double>(vmafFloor - low.first)
    / static_cast<double>(high.first - low.first);
  return low.second + t * (high.second - low.second);
}

auto isHdrVideo(boost::json::value const& vidInfo) -> bool {
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
  if (scores.empty() || isVmafLogEmpty(log)) {
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
      } catch (...) { }
    }
    if (score.has_value()) { scores.push_back(score.value()); }
  }

  if (scores.empty()) {
    return eh::makeError("SSIM stats file contains no scores: {}", statsPath.string());
  }
  return scores;
}

auto measureSegmentQuality(
  fs::path const& ffmpegPath,
  fs::path const& originalPath,
  fs::path const& encodedPath,
  std::uint64_t startUs,
  std::uint64_t durationUs,
  boost::json::value const& originalVideoInfo
) -> eh::Result<SegmentScores> {
  if (durationUs == 0) { return eh::makeError("Segment duration must be non-zero."); }

  auto const logDir = fs::temp_directory_path();
  auto const vmafLog = logDir / std::format("vmaf_{}.json", getUUID());
  auto const ssimStats = logDir / std::format("ssim_{}.txt", getUUID());
  auto const removeLogs = [&] {
    auto ec = std::error_code{};
    fs::remove(vmafLog, ec);
    fs::remove(ssimStats, ec);
  };

  if (!isHdrVideo(originalVideoInfo)) {
    auto const vmafRes =
      runVmaf(ffmpegPath, originalPath, encodedPath, startUs, durationUs, vmafLog);
    if (vmafRes.has_value()) {
      removeLogs();
      return SegmentScores{QualityMetric::Vmaf, std::move(vmafRes.value())};
    }
    LOG_WARN(
      "VMAF unavailable for segment (original={} start={}us); falling back to SSIM: {}",
      originalPath.string(),
      startUs,
      vmafRes.error()
    );
  }

  auto const ssimRes =
    runSsim(ffmpegPath, originalPath, encodedPath, startUs, durationUs, ssimStats);
  removeLogs();
  if (!ssimRes) {
    return eh::makeError(
      "Quality scores unparsable for segment of {}: {}",
      originalPath.string(),
      ssimRes.error()
    );
  }
  return SegmentScores{QualityMetric::Ssim, std::move(ssimRes.value())};
}

}  // namespace videoquality
