// Preprocessing command construction and the real-ffmpeg contract (task 2.2).
#include "tagger/preprocess.h"

#include "test_utils.h"
#include "utils/utils.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("buildPreprocessCommand pins the wd contract", "[tagger]") {
  auto const cmd =
    tagger::buildPreprocessCommand(fs::path{"ffmpeg"}, fs::path{R"(C:\pics\img 01.png)"});

  // White base canvas at the fixed edge size, overlay-centered, raw RGB out.
  CHECK(cmd.find("-f lavfi -i color=c=white:s=448x448") != std::string::npos);
  CHECK(cmd.find("overlay=x=(W-w)/2:y=(H-h)/2") != std::string::npos);
  CHECK(cmd.find("force_original_aspect_ratio=decrease") != std::string::npos);
  CHECK(cmd.find("format=rgb24") != std::string::npos);
  CHECK(cmd.find("-f rawvideo -") != std::string::npos);
  CHECK(cmd.find("-loglevel quiet") != std::string::npos);
  // Quoted input path survives spaces.
  CHECK(cmd.find(R"("C:\pics\img 01.png")") != std::string::npos);
}

TEST_CASE("preprocess contract on a real ffmpeg", "[tagger][real-ffmpeg]") {
  auto const ffmpeg = findFFmpeg(std::nullopt);
  if (!ffmpeg.has_value()) { SKIP("System FFmpeg not available on PATH."); }

  auto temp = TempDir{};
  // 100x50 solid red landscape source; padding must add white bands.
  auto const input = temp.path / "red.png";
  auto const [exitCode, output, _] = exec2(
    std::format(
      R"("{}" -hide_banner -loglevel error -y -f lavfi -i color=c=red:s=100x50 -frames:v 1 "{}")",
      quoteToolPath(*ffmpeg),
      input.string()
    )
  );
  REQUIRE(exitCode == 0);

  auto const res = tagger::runPreprocess(*ffmpeg, input);
  REQUIRE(res.has_value());
  REQUIRE(res->size() == tagger::kInputBytes);

  // Top-left corner is white padding; the exact center is red; RGB order.
  CHECK((*res)[0] == 255);
  CHECK((*res)[1] == 255);
  CHECK((*res)[2] == 255);

  auto const center =
    (tagger::kInputEdge / 2 * tagger::kInputEdge + tagger::kInputEdge / 2) * 3;
  // lavfi's red source is yuv420p, so the RGB->YUV->RGB roundtrip lands a
  // couple of levels below 255; the contract is RGB order + red interior.
  CHECK((*res)[center] >= 250);    // R
  CHECK((*res)[center + 1] == 0);  // G
  CHECK((*res)[center + 2] == 0);  // B
}
