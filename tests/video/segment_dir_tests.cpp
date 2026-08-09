#include "video/segment_dir.h"
#include "test_utils.h"


#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

TEST_CASE("Segment dir path is deterministic per task id", "[segment-dir]") {
  auto const first = videoseg::segmentDirForTask("encode:same-task");
  auto const second = videoseg::segmentDirForTask("encode:same-task");
  auto const other = videoseg::segmentDirForTask("encode:other-task");

  CHECK(first == second);
  CHECK_FALSE(first == other);
  CHECK(first.filename().string().starts_with("segments_"));
  CHECK(first.parent_path().filename() == "encro");
}

TEST_CASE("Segment dir is created on demand and removed recursively", "[segment-dir]") {
  auto const dir = videoseg::segmentDirForTask("encode:create-remove-test");
  videoseg::removeSegmentDir(dir);

  videoseg::createSegmentDir(dir);
  REQUIRE(fs::exists(dir));
  testutils::writeFile(dir / "seg_0.ts");
  testutils::writeFile(dir / "seg_1.ts");
  fs::create_directories(dir / "subdir");
  testutils::writeFile(dir / "subdir" / "audio.m4a");

  videoseg::removeSegmentDir(dir);
  CHECK_FALSE(fs::exists(dir));
}
