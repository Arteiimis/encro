#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace videoseg {

namespace fs = std::filesystem;

// What the segments on disk and the muxer's live list mean for one task
// attempt: which segments are reusable, where to resume, and whether the
// encoder has anything left to do.
struct SegmentPlan {
  std::vector<std::string> reusableNames;  // ordered prefix already on disk
  std::uint64_t resumeUs = 0;              // == segmentMarkUs(reusableNames.size())
  std::uint64_t segmentTotal = 0;          // marks the timeline implies
  bool complete = false;                   // list covers the timeline: assemble only
  std::uint64_t baseFrameOffset = 0;       // frames the bar already counts
  std::uint64_t startNumber() const {
    return static_cast<std::uint64_t>(reusableNames.size());
  }
};

// The one segment-mark arithmetic, shared by the entry and exit directions.
std::uint64_t segmentMarkUs(std::uint64_t segmentCount);

// Exit direction: how many segments the encoder has closed so far, counting the
// prefix an earlier attempt recorded. Reads no state of its own.
std::uint64_t closedSegments(fs::path const& listPath, std::uint64_t startNumber);

// Entry direction: duration plus recorded progress, the segment directory, and
// the probed frame count. Reads and computes; it does not write, spawn or lock.
auto planSegments(
  std::uint64_t totalDurationUs,
  std::uint64_t storedSegments,
  fs::path const& segmentDir,
  std::int64_t totalFrames
) -> SegmentPlan;

}  // namespace videoseg
