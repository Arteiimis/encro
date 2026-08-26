#include "video/video_quality.h"

#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <boost/json.hpp>

#include <array>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr auto kVmafFixture = R"({
  "version": "2.3.1",
  "fps": 25.0,
  "frames": [
    {"frameNum": 0, "metrics": {"vmaf": 96.5}},
    {"frameNum": 1, "metrics": {"vmaf": 97.1}},
    {"frameNum": 2, "metrics": {"vmaf": 94.2}},
    {"frameNum": 3, "metrics": {"vmaf": 95.8}},
    {"frameNum": 4, "metrics": {"vmaf": 93.4}},
    {"frameNum": 5, "metrics": {"vmaf": 96.9}},
    {"frameNum": 6, "metrics": {"vmaf": 95.2}},
    {"frameNum": 7, "metrics": {"vmaf": 94.8}},
    {"frameNum": 8, "metrics": {"vmaf": 97.6}},
    {"frameNum": 9, "metrics": {"vmaf": 95.5}}
  ],
  "pooled_metrics": {
    "vmaf": {"min": 93.4, "max": 97.6, "mean": 95.7, "harmonic_mean": 95.7}
  }
})";

constexpr auto kSsimFixture =
  "n:1 mse_avg:12.34 mse_y:10.00 mse_u:3.00 mse_v:2.00 ssim:0.984500\n"
  "n:2 mse_avg:14.56 mse_y:12.00 mse_u:4.00 mse_v:3.00 ssim:0.981200\n"
  "n:3 mse_avg:16.78 mse_y:14.00 mse_u:5.00 mse_v:4.00 ssim:0.978900\n";

}  // namespace

TEST_CASE("percentile computes nearest-rank p5 and p50", "[video-quality]") {
  auto const scores =
    std::array{96.5, 97.1, 94.2, 95.8, 93.4, 96.9, 95.2, 94.8, 97.6, 95.5};
  auto const p5 = videoquality::percentile(scores, 5.0);
  REQUIRE(p5.has_value());
  CHECK(*p5 == Catch::Approx(93.4));  // nearest-rank: ceil(0.05*10)=1 → minimum
  auto const p50 = videoquality::percentile(scores, 50.0);
  REQUIRE(p50.has_value());
  CHECK(*p50 == Catch::Approx(95.5));  // ceil(0.5*10)=5 → 5th of sorted
}

TEST_CASE("percentile returns nullopt for empty scores", "[video-quality]") {
  auto const scores = std::array<double, 0>{};
  CHECK_FALSE(videoquality::percentile(scores, 5.0).has_value());
}

TEST_CASE("mean aggregates frame scores", "[video-quality]") {
  auto const scores = std::array{1.0, 2.0, 3.0};
  auto const avg = videoquality::mean(scores);
  REQUIRE(avg.has_value());
  CHECK(*avg == Catch::Approx(2.0));
}

TEST_CASE("ssim floor maps the documented vmaf anchors", "[video-quality]") {
  CHECK(videoquality::ssimFloorForVmafFloor(97) == Catch::Approx(0.985));
  CHECK(videoquality::ssimFloorForVmafFloor(95) == Catch::Approx(0.980));
  CHECK(videoquality::ssimFloorForVmafFloor(90) == Catch::Approx(0.970));
}

TEST_CASE("ssim floor interpolates and clamps outside anchors", "[video-quality]") {
  // 96 is halfway between 95 and 97.
  CHECK(videoquality::ssimFloorForVmafFloor(96) == Catch::Approx(0.9825));
  // Below 90 and above 97 clamp to the anchors.
  CHECK(videoquality::ssimFloorForVmafFloor(80) == Catch::Approx(0.970));
  CHECK(videoquality::ssimFloorForVmafFloor(100) == Catch::Approx(0.985));
}

TEST_CASE(
  "parseVmafLog reads per-frame scores from a recorded fixture",
  "[video-quality]"
) {
  TempDir temp;
  auto const logPath = temp.path / "vmaf.json";
  testutils::writeTextFile(logPath, kVmafFixture);

  auto const scores = videoquality::parseVmafLog(logPath);
  REQUIRE(scores.has_value());
  REQUIRE(scores->size() == 10);
  CHECK(scores->front() == Catch::Approx(96.5));
  CHECK(scores->back() == Catch::Approx(95.5));
}

TEST_CASE("parseVmafLog fails on missing or score-less logs", "[video-quality]") {
  TempDir temp;
  auto const missing = temp.path / "missing.json";
  CHECK_FALSE(videoquality::parseVmafLog(missing).has_value());

  auto const emptyLog = temp.path / "empty.json";
  testutils::writeTextFile(emptyLog, R"({"frames": []})");
  CHECK_FALSE(videoquality::parseVmafLog(emptyLog).has_value());

  auto const malformed = temp.path / "malformed.json";
  testutils::writeTextFile(malformed, "not json");
  CHECK_FALSE(videoquality::parseVmafLog(malformed).has_value());
}

TEST_CASE("parseSsimStats reads per-frame ssim values", "[video-quality]") {
  TempDir temp;
  auto const statsPath = temp.path / "ssim.txt";
  testutils::writeTextFile(statsPath, kSsimFixture);

  auto const scores = videoquality::parseSsimStats(statsPath);
  REQUIRE(scores.has_value());
  REQUIRE(scores->size() == 3);
  CHECK(scores->at(0) == Catch::Approx(0.9845));
  CHECK(scores->at(2) == Catch::Approx(0.9789));
}

TEST_CASE("parseSsimStats reads the modern All: line format", "[video-quality]") {
  // ffmpeg >= 4.x prints "n:1 Y:0.868 U:0.738 V:0.761 All:0.829 (dB)".
  TempDir temp;
  auto const statsPath = temp.path / "ssim_modern.txt";
  testutils::writeTextFile(
    statsPath,
    "n:1 Y:0.868 U:0.738 V:0.761 All:0.829 (dB)\n"
    "n:2 Y:0.870 U:0.740 V:0.763 All:0.831 (dB)\n"
  );

  auto const scores = videoquality::parseSsimStats(statsPath);
  REQUIRE(scores.has_value());
  REQUIRE(scores->size() == 2);
  CHECK(scores->at(0) == Catch::Approx(0.829));
  CHECK(scores->at(1) == Catch::Approx(0.831));
}

TEST_CASE("parseSsimStats fails on missing or score-less files", "[video-quality]") {
  TempDir temp;
  auto const missing = temp.path / "missing.txt";
  CHECK_FALSE(videoquality::parseSsimStats(missing).has_value());

  auto const empty = temp.path / "empty.txt";
  testutils::writeTextFile(empty, "no scores here\n");
  CHECK_FALSE(videoquality::parseSsimStats(empty).has_value());
}

TEST_CASE("isHdrVideo detects 10-bit and HDR transfer curves", "[video-quality]") {
  using namespace boost::json;

  auto const sdr =
    parse(R"({"streams":[{"codec_type":"video","bits_per_raw_sample":8}]})");
  CHECK_FALSE(videoquality::isHdrVideo(sdr));

  auto const depth10 =
    parse(R"({"streams":[{"codec_type":"video","bits_per_raw_sample":10}]})");
  CHECK(videoquality::isHdrVideo(depth10));

  auto const pq =
    parse(R"({"streams":[{"codec_type":"video","color_transfer":"smpte2084"}]})");
  CHECK(videoquality::isHdrVideo(pq));

  auto const noInfo = parse(R"({})");
  CHECK_FALSE(videoquality::isHdrVideo(noInfo));
}

TEST_CASE(
  "measureSegmentQuality reports an error when scoring cannot run",
  "[video-quality]"
) {
  TempDir temp;
  auto const original = temp.path / "original.mp4";
  auto const encoded = temp.path / "encoded.mp4";
  testutils::touchFile(original);
  testutils::touchFile(encoded);

  // A nonexistent ffmpeg path fails the scoring command (and both the VMAF
  // and SSIM attempts), yielding the "scores unparsable" error path.
  auto const res = videoquality::measureSegmentQuality(
    videoquality::QualityRequest{
      .ffmpegPath = temp.path / "no-such-ffmpeg",
      .originalPath = original,
      .encodedPath = encoded,
      .startUs = 0,
      .durationUs = 10'000'000,
    }
  );
  REQUIRE_FALSE(res.has_value());
  CHECK(res.error().find("scores unparsable") != std::string::npos);
}
