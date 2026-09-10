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
};

// One segment recorded by the segment muxer's live CSV list:
// "<file name>,<start seconds>,<end seconds>". The listed times carry the
// encoder's reorder delay, so they are informational only: resume seeks to the
// segment's fixed-duration mark (index * segment duration) instead.
struct SegmentListEntry {
  std::string fileName;
  std::uint64_t startUs;
  std::uint64_t endUs;
};

// Reads the segment list in file order. Malformed lines, and a final line that
// was still being written when read (no trailing newline), are ignored; a
// missing or empty list yields no entries.
auto parseSegmentList(fs::path const& listPath) -> std::vector<SegmentListEntry>;

bool isLikelyFfmpegErrorLine(std::string_view line);

auto readLastNLines(fs::path const& filePath, std::size_t n) -> std::vector<std::string>;

auto parseProgressFile(fs::path const& progressFilePath) -> std::optional<ProgressData>;

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
