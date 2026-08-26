#include "core/app_context.h"
#include "test_utils.h"
#include "video/video_info.h"

#include <cstdint>
#include <fstream>

#if defined(_WIN32)
auto makeCmdScriptCommand(fs::path const& scriptPath) -> fs::path {
  return fs::path{std::format("cmd.exe /d /c call \"{}\"", scriptPath.string())};
}

void writeFakeFfprobeScript(fs::path const& scriptPath) {
  auto const script = R"(
@echo off
setlocal EnableExtensions
echo {"format":{"duration":"2.0"},"streams":[{"codec_type":"video","codec_name":"h264","nb_frames":"10","avg_frame_rate":"5/1"}]}
exit /b 0
)";
  auto out = std::ofstream{scriptPath, std::ios::binary};
  REQUIRE(out.is_open());
  out << script;
}
#endif

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

  auto config = appctx::AppConfig{};
  config.outputFormat = "webp";
  config.recursive = false;
  auto toolchain = appctx::ToolchainPaths{};
  auto runtime = appctx::RuntimeContext{};

  auto const vids = readAllVids(config, toolchain, runtime, largeVideo);

  REQUIRE(vids);
  CHECK(vids->empty());
}

TEST_CASE("readAllVids allows files just below 32MB for webp", "[video-info]") {
  TempDir temp;
  auto const boundaryVideo = temp.path / "boundary.mp4";
  createFileWithSize(boundaryVideo, 32ULL * 1024ULL * 1024ULL - 1ULL);

  auto config = appctx::AppConfig{};
  config.outputFormat = "webp";
  config.recursive = false;
  auto toolchain = appctx::ToolchainPaths{};
  auto runtime = appctx::RuntimeContext{};

  auto const vids = readAllVids(config, toolchain, runtime, boundaryVideo);

  REQUIRE(vids);
  REQUIRE(vids->size() == 1);
  CHECK(vids->front() == boundaryVideo);
}

TEST_CASE("readAllVids for webp prewarms video info cache", "[video-info]") {
  TempDir temp;
  auto const boundaryVideo = temp.path / "boundary.mp4";
  createFileWithSize(boundaryVideo, 1024ULL);

  auto config = appctx::AppConfig{};
  config.outputFormat = "webp";
  config.recursive = false;
  auto toolchain = appctx::ToolchainPaths{};
  auto runtime = appctx::RuntimeContext{};

#if defined(_WIN32)
  auto const ffprobeScriptPath = temp.path / "fake_ffprobe.cmd";
  writeFakeFfprobeScript(ffprobeScriptPath);
  toolchain.ffprobePath = makeCmdScriptCommand(ffprobeScriptPath);
#endif

  auto const vids = readAllVids(config, toolchain, runtime, temp.path);

  REQUIRE(vids);
  REQUIRE(vids->size() == 1);
  CHECK(vids->front() == boundaryVideo);
  CHECK(runtime.videoInfoCache.size() == 1);
  CHECK(runtime.videoInfoCache.find(boundaryVideo).has_value());
}

TEST_CASE(
  "readAllVidsFromFiles for webp prewarms only bounded lookahead entries",
  "[video-info]"
) {
  TempDir temp;
  auto const firstVideo = temp.path / "a.mp4";
  auto const secondVideo = temp.path / "b.mp4";
  auto const thirdVideo = temp.path / "c.mp4";
  createFileWithSize(firstVideo, 1024ULL);
  createFileWithSize(secondVideo, 1024ULL);
  createFileWithSize(thirdVideo, 1024ULL);

  auto config = appctx::AppConfig{};
  config.outputFormat = "webp";
  config.maxParallelJobs = 1;
  auto toolchain = appctx::ToolchainPaths{};
  auto runtime = appctx::RuntimeContext{};

#if defined(_WIN32)
  auto const ffprobeScriptPath = temp.path / "fake_ffprobe.cmd";
  writeFakeFfprobeScript(ffprobeScriptPath);
  toolchain.ffprobePath = makeCmdScriptCommand(ffprobeScriptPath);
#endif

  auto const inputFiles = std::array{firstVideo, secondVideo, thirdVideo};
  auto const vids = readAllVidsFromFiles(config, toolchain, runtime, inputFiles);

  REQUIRE(vids == std::vector<fs::path>{firstVideo, secondVideo, thirdVideo});
  CHECK(runtime.videoInfoCache.size() == 2);
  CHECK(runtime.videoInfoCache.find(firstVideo).has_value());
  CHECK(runtime.videoInfoCache.find(secondVideo).has_value());
  CHECK_FALSE(runtime.videoInfoCache.find(thirdVideo).has_value());
}

TEST_CASE("readAllVids keeps only <32MB videos for webp in directory", "[video-info]") {
  TempDir temp;
  auto const smallVideo = temp.path / "small.mp4";
  auto const largeVideo = temp.path / "large.mp4";
  createFileWithSize(smallVideo, 1024ULL);
  createFileWithSize(largeVideo, 32ULL * 1024ULL * 1024ULL);

  auto config = appctx::AppConfig{};
  config.outputFormat = "webp";
  config.recursive = false;
  auto toolchain = appctx::ToolchainPaths{};
  auto runtime = appctx::RuntimeContext{};

  auto const vids = readAllVids(config, toolchain, runtime, temp.path);

  REQUIRE(vids);
  REQUIRE(vids->size() == 1);
  CHECK(vids->front() == smallVideo);
}

TEST_CASE("getVidTotalFrames reads cached info from shared cache", "[video-info]") {
  auto runtime = appctx::RuntimeContext{};
  auto toolchain = appctx::ToolchainPaths{};
  auto const videoPath = fs::path{"sample.mp4"};

  runtime.videoInfoCache.set(
    videoPath,
    boost::json::parse(R"({"streams":[{"codec_type":"video","nb_frames":"42"}]})")
  );

  auto const totalFrames = getVidTotalFrames(toolchain, runtime, videoPath);

  REQUIRE(totalFrames);
  CHECK(totalFrames.value() == 42);
}

TEST_CASE("getVidTotalDurationUs reads cached format duration", "[video-info]") {
  auto runtime = appctx::RuntimeContext{};
  auto toolchain = appctx::ToolchainPaths{};
  auto const videoPath = fs::path{"sample.mp4"};

  runtime.videoInfoCache
    .set(videoPath, boost::json::parse(R"({"format":{"duration":"2.5"}})"));

  auto const durationUs = getVidTotalDurationUs(toolchain, runtime, videoPath);

  REQUIRE(durationUs);
  CHECK(durationUs.value() == 2'500'000);
}

TEST_CASE("getVidTotalDurationUs errors when duration missing", "[video-info]") {
  auto runtime = appctx::RuntimeContext{};
  auto toolchain = appctx::ToolchainPaths{};
  auto const videoPath = fs::path{"sample.mp4"};

  runtime.videoInfoCache.set(videoPath, boost::json::parse(R"({"format":{}})"));

  auto const durationUs = getVidTotalDurationUs(toolchain, runtime, videoPath);

  REQUIRE_FALSE(durationUs);
}

TEST_CASE("getVidDimensions reads cached video stream dimensions", "[video-info]") {
  auto runtime = appctx::RuntimeContext{};
  auto toolchain = appctx::ToolchainPaths{};
  auto const videoPath = fs::path{"sample.mp4"};

  runtime.videoInfoCache.set(
    videoPath,
    boost::json::parse(
      R"({"streams":[{"codec_type":"audio"},{"codec_type":"video","width":2560,"height":1440}]})"
    )
  );

  auto const dims = getVidDimensions(toolchain, runtime, videoPath);

  REQUIRE(dims);
  CHECK(dims->first == 2560);
  CHECK(dims->second == 1440);
}

TEST_CASE("getVidDimensions errors when width missing", "[video-info]") {
  auto runtime = appctx::RuntimeContext{};
  auto toolchain = appctx::ToolchainPaths{};
  auto const videoPath = fs::path{"sample.mp4"};

  runtime.videoInfoCache.set(
    videoPath,
    boost::json::parse(R"({"streams":[{"codec_type":"video","height":1440}]})")
  );

  auto const dims = getVidDimensions(toolchain, runtime, videoPath);

  REQUIRE_FALSE(dims);
}

TEST_CASE("getVidHasAudio detects audio stream", "[video-info]") {
  auto runtime = appctx::RuntimeContext{};
  auto toolchain = appctx::ToolchainPaths{};
  auto const videoPath = fs::path{"sample.mp4"};

  runtime.videoInfoCache.set(
    videoPath,
    boost::json::parse(
      R"({"streams":[{"codec_type":"video"},{"codec_type":"audio","codec_name":"aac"}]})"
    )
  );

  auto const hasAudio = getVidHasAudio(toolchain, runtime, videoPath);

  REQUIRE(hasAudio);
  CHECK(hasAudio.value());
}

TEST_CASE("getVidHasAudio false without audio stream", "[video-info]") {
  auto runtime = appctx::RuntimeContext{};
  auto toolchain = appctx::ToolchainPaths{};
  auto const videoPath = fs::path{"sample.mp4"};

  runtime.videoInfoCache
    .set(videoPath, boost::json::parse(R"({"streams":[{"codec_type":"video"}]})"));

  auto const hasAudio = getVidHasAudio(toolchain, runtime, videoPath);

  REQUIRE(hasAudio);
  CHECK_FALSE(hasAudio.value());
}
