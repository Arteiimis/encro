#include "globals.h"
#include "test_utils.h"
#include "video_info.h"

#include <catch2/catch_all.hpp>

#include <cstdint>
#include <fstream>

namespace fs = std::filesystem;

namespace {

void createFileWithSize(fs::path const& filePath, std::uintmax_t sizeInBytes) {
  auto file = std::ofstream{filePath, std::ios::binary};
  REQUIRE(file.is_open());

  if (sizeInBytes == 0) {
    file.flush();
    return;
  }

  file.seekp(static_cast<std::streamoff>(sizeInBytes - 1));
  file.put('\0');
  file.flush();
}

}  // namespace

TEST_CASE("readAllVids skips files at or above 32MB for webp", "[video-info]") {
  TempDir temp;
  auto const largeVideo = temp.path / "large.mp4";
  createFileWithSize(largeVideo, 32ULL * 1024ULL * 1024ULL);

  GLBs.OUTPUT_FORMAT = "webp";
  GLBs.RECURSIVE = false;
  GLBs.VIDEO_INFO_CACHE.clear();

  auto const vids = readAllVids(largeVideo);

  REQUIRE(vids.empty());
}

TEST_CASE("readAllVids allows files just below 32MB for webp", "[video-info]") {
  TempDir temp;
  auto const boundaryVideo = temp.path / "boundary.mp4";
  createFileWithSize(boundaryVideo, 32ULL * 1024ULL * 1024ULL - 1ULL);

  GLBs.OUTPUT_FORMAT = "webp";
  GLBs.RECURSIVE = false;
  GLBs.VIDEO_INFO_CACHE.clear();

  auto const vids = readAllVids(boundaryVideo);

  REQUIRE(vids.size() == 1);
  CHECK(vids.front() == boundaryVideo);
}

TEST_CASE(
  "readAllVids keeps only <32MB videos for webp in directory",
  "[video-info]"
) {
  TempDir temp;
  auto const smallVideo = temp.path / "small.mp4";
  auto const largeVideo = temp.path / "large.mp4";
  createFileWithSize(smallVideo, 1024ULL);
  createFileWithSize(largeVideo, 32ULL * 1024ULL * 1024ULL);

  GLBs.OUTPUT_FORMAT = "webp";
  GLBs.RECURSIVE = false;
  GLBs.VIDEO_INFO_CACHE.clear();

  auto const vids = readAllVids(temp.path);

  REQUIRE(vids.size() == 1);
  CHECK(vids.front() == smallVideo);
}
