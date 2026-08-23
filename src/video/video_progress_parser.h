#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

struct ProgressData {
  uint64_t frameCount;
  std::string status;
};

bool isLikelyFfmpegErrorLine(std::string_view line);

auto readLastNLines(fs::path const& filePath, std::size_t n) -> std::vector<std::string>;

auto parseProgressFile(fs::path const& progressFilePath) -> std::optional<ProgressData>;

auto parseSegmentEndUs(fs::path const& progressFilePath) -> std::optional<std::uint64_t>;

inline std::uint64_t segmentBaseFrameOffset(
  std::uint64_t cumulativeDurationUs,
  std::int64_t totalFrames,
  std::uint64_t totalDurationUs
) {
  if (totalFrames <= 0 || totalDurationUs == 0) { return 0; }
  return static_cast<std::uint64_t>(std::llround(  // NOLINT(bugprone-narrowing-conversions): frame math needs double
    static_cast<double>(cumulativeDurationUs)
    * static_cast<double>(totalFrames)
    / static_cast<double>(totalDurationUs)
  ));
}

inline float progressPercent(
  std::uint64_t frameCount,
  std::uint64_t baseFrameOffset,
  std::int64_t totalFrames
) {
  if (totalFrames <= 0) { return 0.0f; }
  return std::min(
    (static_cast<float>(baseFrameOffset + frameCount) / static_cast<float>(totalFrames))
      * 100.0f,
    100.0f
  );
}
