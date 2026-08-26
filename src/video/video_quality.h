#pragma once

#include "core/error_handle.h"

#include <boost/json.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace videoquality {

enum class QualityMetric {
  Vmaf,
  Ssim,
  Xpsnr
};

struct SegmentScores {
  QualityMetric metric;
  std::vector<double> frameScores;
};

// One aligned original-vs-encoded segment to score: the two paths, the time
// range, and the input metadata the metric decision needs.
struct QualityRequest {
  std::filesystem::path ffmpegPath;
  std::filesystem::path originalPath;
  std::filesystem::path encodedPath;
  std::uint64_t startUs;
  std::uint64_t durationUs;
  boost::json::value originalVideoInfo = boost::json::value{};
  bool encodedHasLocalPts =
    false;  // probe segments already seeked; only original is re-seeked
};

// p-th percentile of the scores (nearest-rank); nullopt for empty input.
auto percentile(std::span<double const> scores, double percentile)
  -> std::optional<double>;

auto mean(std::span<double const> scores) -> std::optional<double>;

// Display name for a quality metric ("VMAF" | "SSIM" | "XPSNR").
auto metricName(QualityMetric metric) -> std::string_view;

// Maps a VMAF floor to the equivalent SSIM floor via the anchor points
// 97->0.985 / 95->0.980 / 90->0.970 (piecewise-linear, clamped outside).
double ssimFloorForVmafFloor(int vmafFloor);

// Maps a VMAF floor onto the equivalent XPSNR threshold in dB via the anchors
// 97->42.5 / 95->41.0 / 90->38.5 (piecewise-linear, clamped outside).
double xpsnrFloorForVmafFloor(int vmafFloor);

// True when the probed video is HDR (bit depth > 8 or HDR transfer curve).
bool isHdrVideo(boost::json::value const& vidInfo);

// Parses a libvmaf JSON log (log_fmt=json) into per-frame VMAF scores.
auto parseVmafLog(std::filesystem::path const& logPath)
  -> eh::Result<std::vector<double>>;

// Parses an ssim filter stats_file into per-frame SSIM scores.
auto parseSsimStats(std::filesystem::path const& statsPath)
  -> eh::Result<std::vector<double>>;

// Parses an xpsnr filter stats_file into per-frame XPSNR dB scores (each
// frame's score is the mean of its plane values).
auto parseXpsnrStats(std::filesystem::path const& statsPath)
  -> eh::Result<std::vector<double>>;

// Decodes the aligned segment pair (original vs encoded) and runs XPSNR for
// non-HDR inputs, falling back to libvmaf and then ssim (HDR skips straight
// to ssim). Returns the per-frame scores in the metric actually used.
auto measureSegmentQuality(QualityRequest const& request) -> eh::Result<SegmentScores>;

}  // namespace videoquality
