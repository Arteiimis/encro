#include "core/app_context.h"
#include "test_utils.h"
#include "video/video_info.h"

#include <cstdint>
#include <fstream>

using testutils::copyFakeProbe;
using testutils::copyFakeTool;

namespace fs = std::filesystem;

TEST_CASE("readAllVids skips files at or above 32MB for webp", "[video-info]") {
  TempDir temp;
  auto const largeVideo = temp.path / "large.mp4";
  testutils::writeSizedFile(largeVideo, 32ULL * 1024ULL * 1024ULL);

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
  testutils::writeSizedFile(boundaryVideo, 32ULL * 1024ULL * 1024ULL - 1ULL);

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
  testutils::writeSizedFile(boundaryVideo, 1024ULL);

  auto config = appctx::AppConfig{};
  config.outputFormat = "webp";
  config.recursive = false;
  auto toolchain = appctx::ToolchainPaths{};
  auto runtime = appctx::RuntimeContext{};

  toolchain.ffprobePath = copyFakeTool(temp.path, "ffprobe");
  auto const probeEnv = copyFakeProbe(temp.path);

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
  testutils::writeSizedFile(firstVideo, 1024ULL);
  testutils::writeSizedFile(secondVideo, 1024ULL);
  testutils::writeSizedFile(thirdVideo, 1024ULL);

  auto config = appctx::AppConfig{};
  config.outputFormat = "webp";
  config.maxParallelJobs = 1;
  auto toolchain = appctx::ToolchainPaths{};
  auto runtime = appctx::RuntimeContext{};

  toolchain.ffprobePath = copyFakeTool(temp.path, "ffprobe");
  auto const probeEnv = copyFakeProbe(temp.path);

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
  testutils::writeSizedFile(smallVideo, 1024ULL);
  testutils::writeSizedFile(largeVideo, 32ULL * 1024ULL * 1024ULL);

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

TEST_CASE(
  "getVidTotalFrames fallback chain handles guarded parse corners",
  "[video-info]"
) {
  auto runtime = appctx::RuntimeContext{};
  auto toolchain = appctx::ToolchainPaths{};
  auto const videoPath = fs::path{"cached-sample.mp4"};

  auto seed = [&](std::string_view json) {
    runtime.videoInfoCache.set(videoPath, boost::json::parse(json));
  };

  SECTION("string nb_frames N/A falls through to the rate fallback") {
    seed(
      R"({"format":{"duration":"2.0"},"streams":[{"codec_type":"video","nb_frames":"N/A","avg_frame_rate":"25/1"}]})"
    );
    auto const frames = getVidTotalFrames(toolchain, runtime, videoPath);
    REQUIRE(frames);
    CHECK(frames.value() == 50);
  }

  SECTION("empty nb_frames falls through to the rate fallback") {
    seed(
      R"({"format":{"duration":"2.0"},"streams":[{"codec_type":"video","nb_frames":"","avg_frame_rate":"25/1"}]})"
    );
    auto const frames = getVidTotalFrames(toolchain, runtime, videoPath);
    REQUIRE(frames);
    CHECK(frames.value() == 50);
  }

  SECTION("missing nb_frames uses the avg_frame_rate fraction") {
    seed(
      R"({"format":{"duration":"2.0"},"streams":[{"codec_type":"video","avg_frame_rate":"25/1"}]})"
    );
    auto const frames = getVidTotalFrames(toolchain, runtime, videoPath);
    REQUIRE(frames);
    CHECK(frames.value() == 50);
  }

  SECTION("zero-denominator avg_frame_rate is rejected, not thrown") {
    seed(
      R"({"format":{"duration":"2.0"},"streams":[{"codec_type":"video","nb_frames":"N/A","avg_frame_rate":"0/0","r_frame_rate":"25/1"}]})"
    );
    auto const frames = getVidTotalFrames(toolchain, runtime, videoPath);
    REQUIRE(frames);
    CHECK(frames.value() == 50);
  }

  SECTION("negative avg_frame_rate propagates a negative count") {
    // parseFraction accepts a leading sign; the caller does not reject it.
    seed(
      R"({"format":{"duration":"2.0"},"streams":[{"codec_type":"video","avg_frame_rate":"-25/1"}]})"
    );
    auto const frames = getVidTotalFrames(toolchain, runtime, videoPath);
    REQUIRE(frames);
    CHECK(frames.value() == -50);
  }

  SECTION("oversized numerator stays representable") {
    seed(
      R"({"format":{"duration":"2.0"},"streams":[{"codec_type":"video","avg_frame_rate":"1000000000/1"}]})"
    );
    auto const frames = getVidTotalFrames(toolchain, runtime, videoPath);
    REQUIRE(frames);
    CHECK(frames.value() == 2'000'000'000);
  }

  SECTION("empty streams array errors") {
    seed(R"({"format":{"duration":"2.0"},"streams":[]})");
    auto const frames = getVidTotalFrames(toolchain, runtime, videoPath);
    REQUIRE_FALSE(frames);
  }

  SECTION("unparseable rates with no fallback error out") {
    seed(
      R"({"format":{"duration":"N/A"},"streams":[{"codec_type":"video","nb_frames":"N/A","avg_frame_rate":"0/0"}]})"
    );
    auto const frames = getVidTotalFrames(toolchain, runtime, videoPath);
    REQUIRE_FALSE(frames);
  }
}

TEST_CASE("getVidTotalDurationUs rejects guarded non-numeric corners", "[video-info]") {
  auto runtime = appctx::RuntimeContext{};
  auto toolchain = appctx::ToolchainPaths{};
  auto const videoPath = fs::path{"cached-sample.mp4"};

  auto seed = [&](std::string_view json) {
    runtime.videoInfoCache.set(videoPath, boost::json::parse(json));
  };

  SECTION("N/A duration errors") {
    seed(R"({"format":{"duration":"N/A"}})");
    auto const durationUs = getVidTotalDurationUs(toolchain, runtime, videoPath);
    REQUIRE_FALSE(durationUs);
  }

  SECTION("empty duration errors") {
    seed(R"({"format":{"duration":""}})");
    auto const durationUs = getVidTotalDurationUs(toolchain, runtime, videoPath);
    REQUIRE_FALSE(durationUs);
  }

  SECTION("non-object cache entry errors") {
    runtime.videoInfoCache.set(videoPath, boost::json::parse(R"(42)"));
    auto const durationUs = getVidTotalDurationUs(toolchain, runtime, videoPath);
    REQUIRE_FALSE(durationUs);
  }
}

TEST_CASE("readAllVidsFromFiles filters unknown extensions", "[video-info]") {
  TempDir temp;
  auto const video = temp.path / "a.mkv";
  auto const notAVideo = temp.path / "notes.txt";
  testutils::writeSizedFile(video, 1024ULL);
  testutils::writeTextFile(notAVideo, "text");

  auto config = appctx::AppConfig{};
  config.outputFormat = "mp4";
  auto toolchain = appctx::ToolchainPaths{};
  auto runtime = appctx::RuntimeContext{};

  auto const inputFiles = std::array{video, notAVideo};
  auto const vids = readAllVidsFromFiles(config, toolchain, runtime, inputFiles);

  CHECK(vids == std::vector<fs::path>{video});
}
