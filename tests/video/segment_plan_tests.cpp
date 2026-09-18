#include "test_utils.h"
#include "video/encode_config.h"
#include "video/segment_plan.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A 25 s timeline: three segment marks (10 s, 20 s, 25 s), of which the muxer's
// cut cadence may close fewer.
constexpr auto kTimelineUs = std::uint64_t{25'000'000};

void writeSegmentList(fs::path const& listPath, std::vector<std::string> const& rows) {
  auto out = std::ofstream{listPath};
  REQUIRE(out.is_open());
  for (auto const& row: rows) { out << row << "\n"; }
}

void writeSegmentFiles(
  fs::path const& segmentDir,
  std::vector<std::uint64_t> const& indexes
) {
  for (auto const index: indexes) {
    testutils::writeTextFile(segmentDir / segmentFileName(index));
  }
}

}  // namespace

TEST_CASE(
  "planSegments stops the reusable prefix at a missing segment file",
  "[segment-plan]"
) {
  TempDir temp;
  writeSegmentFiles(temp.path, {0, 2});
  writeSegmentList(segmentListPath(temp.path), {"seg_0.ts,0.000000,10.000000"});

  auto const plan = videoseg::planSegments(kTimelineUs, 3, temp.path, 0);

  // Three marks are recorded, but seg_1.ts vanished: only seg_0.ts is reusable
  // and the run restarts from the 10 s mark.
  CHECK(plan.reusableNames == std::vector<std::string>{"seg_0.ts"});
  CHECK(plan.startNumber() == 1);
  CHECK(plan.resumeUs == 10'000'000);
  CHECK(plan.segmentTotal == 3);
  CHECK_FALSE(plan.complete);
}

TEST_CASE(
  "planSegments completes when the muxer list reaches the timeline end",
  "[segment-plan]"
) {
  TempDir temp;
  writeSegmentFiles(temp.path, {0, 1});
  writeSegmentList(
    segmentListPath(temp.path),
    {
      "seg_0.ts,0.000000,10.125000",
      "seg_1.ts,10.125000,25.500000",
    }
  );

  auto const plan = videoseg::planSegments(kTimelineUs, 2, temp.path, 0);

  // Two marks on disk against the three the duration implies: the list ends at
  // the timeline, so the encoder cut fewer segments than the duration implies
  // and there is nothing left to encode.
  CHECK(plan.startNumber() == 2);
  CHECK(plan.segmentTotal == 3);
  CHECK(plan.complete);
}

TEST_CASE("planSegments leaves an interrupted series incomplete", "[segment-plan]") {
  TempDir temp;
  writeSegmentFiles(temp.path, {0, 1});
  writeSegmentList(
    segmentListPath(temp.path),
    {
      "seg_0.ts,0.000000,10.000000",
      "seg_1.ts,10.000000,20.000000",
    }
  );

  auto const plan = videoseg::planSegments(kTimelineUs, 2, temp.path, 0);

  // The list stops a whole segment short of the timeline: a run died with its
  // third segment in flight, so the tail still has to be encoded.
  CHECK_FALSE(plan.complete);
  CHECK(plan.resumeUs == 20'000'000);
}

TEST_CASE(
  "planSegments converts the resume point into the bar's frame offset",
  "[segment-plan]"
) {
  TempDir temp;
  writeSegmentFiles(temp.path, {0, 1});

  SECTION("probed frames map the resume point onto the timeline") {
    auto const plan = videoseg::planSegments(30'000'000, 2, temp.path, 2700);

    CHECK(plan.resumeUs == 20'000'000);
    CHECK(plan.baseFrameOffset == 1800);
  }

  SECTION("a failed frame probe leaves the offset at zero") {
    auto const plan = videoseg::planSegments(30'000'000, 2, temp.path, 0);

    CHECK(plan.resumeUs == 20'000'000);
    CHECK(plan.baseFrameOffset == 0);
  }
}

TEST_CASE(
  "closedSegments counts the muxer rows after the resumed prefix",
  "[segment-plan]"
) {
  TempDir temp;
  auto const listPath = segmentListPath(temp.path);
  writeSegmentList(
    listPath,
    {
      "seg_2.ts,0.000000,10.000000",
      "seg_3.ts,10.000000,20.000000",
    }
  );

  // This attempt's rows continue the numbering after the two recorded marks.
  CHECK(videoseg::closedSegments(listPath, 2) == 4);
  // A list the muxer has not created yet closes nothing of this attempt.
  CHECK(videoseg::closedSegments(temp.path / "missing.csv", 2) == 2);
}
