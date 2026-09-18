#include "video/segment_plan.h"

#include "video/encode_config.h"
#include "video/video_progress_parser.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace videoseg {

namespace {

// Segments an earlier attempt already finished, in order. The recorded count is
// the authority: the muxer rewrites its list for every attempt, so the list only
// ever describes the last one, while job state accumulates the total. Names are
// derived from the index because the muxer updates list rows in place and may
// pad a name with spaces, while the files follow the naming pattern.
auto reusableSegments(fs::path const& segmentDir, std::uint64_t storedSegments)
  -> std::vector<std::string> {
  auto reusable = std::vector<std::string>{};
  for (auto index = std::uint64_t{0}; index < storedSegments; ++index) {
    auto const name = segmentFileName(index);
    if (!fs::exists(segmentDir / name)) { break; }
    reusable.push_back(name);
  }
  return reusable;
}

// True when the encoder has nothing left to do: every segment mark is already on
// disk, or the muxer's cut cadence produced fewer segments than the duration
// implies and its list reaches the end of the timeline. Listed times carry the
// encoder's reorder delay, so a complete list ends at or just past the duration
// while a run that died with a segment in flight stops a whole segment short.
bool encodeComplete(
  std::uint64_t completed,
  std::uint64_t segmentTotal,
  std::span<SegmentListEntry const> listedSegments,
  std::uint64_t totalDurationUs
) {
  if (completed >= segmentTotal) { return true; }
  return !listedSegments.empty()
    && completed >= listedSegments.size()
    && listedSegments.back().endUs >= totalDurationUs;
}

}  // namespace

std::uint64_t segmentMarkUs(std::uint64_t segmentCount) {
  return segmentCount * kSegmentDurationUs;
}

std::uint64_t closedSegments(fs::path const& listPath, std::uint64_t startNumber) {
  return startNumber + static_cast<std::uint64_t>(parseSegmentList(listPath).size());
}

auto planSegments(
  std::uint64_t totalDurationUs,
  std::uint64_t storedSegments,
  fs::path const& segmentDir,
  std::int64_t totalFrames
) -> SegmentPlan {
  auto plan = SegmentPlan{};
  plan.reusableNames = reusableSegments(segmentDir, storedSegments);
  plan.resumeUs = segmentMarkUs(plan.startNumber());
  plan.segmentTotal = (totalDurationUs + kSegmentDurationUs - 1) / kSegmentDurationUs;
  plan.complete = encodeComplete(
    plan.startNumber(),
    plan.segmentTotal,
    parseSegmentList(segmentListPath(segmentDir)),
    totalDurationUs
  );
  plan.baseFrameOffset =
    segmentBaseFrameOffset(plan.resumeUs, totalFrames, totalDurationUs);
  return plan;
}

}  // namespace videoseg
