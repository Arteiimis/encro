#include "video/encode_config.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static auto createTempFile(fs::path const& dir, std::string_view name) {
  auto const filePath = dir / name;
  std::ofstream file{filePath};
  file << "dummy";
  return filePath;
}

TEST_CASE("EncodeConfig validates required fields", "[encode-config]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mp4");

  EncodeConfig cfg;
  cfg.inputPath = inputPath;
  cfg.crf = 22;
  cfg.progressFilePath = temp.path / "progress.log";

  auto const validation = cfg.validate();
  REQUIRE(validation);

  auto const cmd = cfg.buildCMD();
  CHECK(cmd.find(inputPath.string()) != std::string::npos);
  CHECK(cmd.find("-c:v hevc_nvenc -crf 22") != std::string::npos);
  CHECK(cmd.find(cfg.progressFilePath->string()) != std::string::npos);
  auto const expectedOutput =
    (inputPath.parent_path() / std::format("{}.hevc.mp4", inputPath.stem().string()))
      .string();
  CHECK(cmd.find(expectedOutput) != std::string::npos);
}

TEST_CASE("EncodeConfig rejects missing input", "[encode-config]") {
  EncodeConfig cfg;
  auto const validation = cfg.validate();

  REQUIRE_FALSE(validation);
  CHECK(validation.error().find("Input path is required") != std::string::npos);
}

TEST_CASE("EncodeConfig rejects invalid CRF", "[encode-config]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mp4");

  EncodeConfig cfg;
  cfg.inputPath = inputPath;
  cfg.crf = 100;

  auto const validation = cfg.validate();

  REQUIRE_FALSE(validation);
  CHECK(
    validation.error().find("CRF value must be between 0 and 51") != std::string::npos
  );
}

TEST_CASE("EncodeConfig rejects empty output format", "[encode-config]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mp4");

  EncodeConfig cfg;
  cfg.inputPath = inputPath;
  cfg.outputFormat = std::string{};

  auto const validation = cfg.validate();

  REQUIRE_FALSE(validation);
  CHECK(
    validation.error().find("Output format cannot be an empty string")
    != std::string::npos
  );
}

TEST_CASE("EncodeConfig builds webp command", "[encode-config]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mp4");

  EncodeConfig cfg;
  cfg.inputPath = inputPath;
  cfg.outputFormat = "webp";

  auto const validation = cfg.validate();
  REQUIRE(validation);

  auto const cmd = cfg.buildCMD();
  CHECK(cmd.find("-hide_banner") != std::string::npos);
  CHECK(cmd.find("-nostats") != std::string::npos);
  CHECK(cmd.find("-loglevel error") != std::string::npos);
  CHECK(cmd.find("-c:v libwebp") != std::string::npos);
  CHECK(
    cmd.find("-vf \"scale=-2:960:force_original_aspect_ratio=decrease\"")
    != std::string::npos
  );
  CHECK(cmd.find("-crf") == std::string::npos);

  auto const expectedOutput =
    (inputPath.parent_path() / std::format("{}.webp", inputPath.stem().string()))
      .string();
  CHECK(cmd.find(expectedOutput) != std::string::npos);
}

TEST_CASE("EncodeConfig rejects unsupported output format", "[encode-config]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mp4");

  EncodeConfig cfg;
  cfg.inputPath = inputPath;
  cfg.outputFormat = "mkv";

  auto const validation = cfg.validate();

  REQUIRE_FALSE(validation);
  CHECK(
    validation.error().find("Output format must be one of: mp4, webp")
    != std::string::npos
  );
}

TEST_CASE(
  "EncodeConfig uses codec prefix in non-webp output filename",
  "[encode-config]"
) {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mov");

  EncodeConfig cfg;
  cfg.inputPath = inputPath;
  cfg.outputFormat = "mp4";
  cfg.videoCodec = "h264_nvenc";

  auto const validation = cfg.validate();
  REQUIRE(validation);

  auto const cmd = cfg.buildCMD();
  CHECK(cmd.find("-c:v h264_nvenc") != std::string::npos);

  auto const expectedOutput =
    (inputPath.parent_path() / std::format("{}.h264.mp4", inputPath.stem().string()))
      .string();
  CHECK(cmd.find(expectedOutput) != std::string::npos);
}

TEST_CASE(
  "EncodeConfig keeps full codec tag when codec has no underscore",
  "[encode-config]"
) {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mov");

  EncodeConfig cfg;
  cfg.inputPath = inputPath;
  cfg.outputFormat = "mp4";
  cfg.videoCodec = "vp9";

  auto const validation = cfg.validate();
  REQUIRE(validation);

  auto const cmd = cfg.buildCMD();
  auto const expectedOutput =
    (inputPath.parent_path() / std::format("{}.vp9.mp4", inputPath.stem().string()))
      .string();
  CHECK(cmd.find(expectedOutput) != std::string::npos);
}

TEST_CASE("EncodeConfig respects custom output directory", "[encode-config]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mp4");
  auto const outputDir = temp.path / "encoded";
  fs::create_directory(outputDir);

  EncodeConfig cfg;
  cfg.inputPath = inputPath;
  cfg.outputPath = outputDir;
  cfg.outputFormat = "mp4";
  cfg.videoCodec = "hevc_nvenc";

  auto const validation = cfg.validate();
  REQUIRE(validation);

  auto const cmd = cfg.buildCMD();
  auto const expectedOutput =
    (outputDir / std::format("{}.hevc.mp4", inputPath.stem().string())).string();
  CHECK(cmd.find(expectedOutput) != std::string::npos);
}

TEST_CASE("EncodeConfig respects explicit output file path", "[encode-config]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mp4");
  auto const outputFile = temp.path / "nested" / "sample.custom.webp";

  EncodeConfig cfg;
  cfg.inputPath = inputPath;
  cfg.outputFormat = "webp";
  cfg.outputFilePath = outputFile;

  auto const validation = cfg.validate();
  REQUIRE(validation);

  CHECK(cfg.buildOutputPath() == outputFile);
  CHECK(cfg.buildCMD().find(outputFile.string()) != std::string::npos);
}

TEST_CASE(
  "EncodeConfig webp output filename does not include codec tag",
  "[encode-config]"
) {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mp4");

  EncodeConfig cfg;
  cfg.inputPath = inputPath;
  cfg.outputFormat = "webp";
  cfg.videoCodec = "h264_nvenc";

  auto const validation = cfg.validate();
  REQUIRE(validation);

  auto const cmd = cfg.buildCMD();
  auto const expectedOutput =
    (inputPath.parent_path() / std::format("{}.webp", inputPath.stem().string()))
      .string();

  CHECK(cmd.find(expectedOutput) != std::string::npos);
  CHECK(cmd.find(".h264.webp") == std::string::npos);
}

TEST_CASE("EncodeConfig uses custom webp quality", "[encode-config]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mp4");

  EncodeConfig cfg;
  cfg.inputPath = inputPath;
  cfg.outputFormat = "webp";
  cfg.webpQuality = 55;

  auto const validation = cfg.validate();
  REQUIRE(validation);

  auto const cmd = cfg.buildCMD();
  CHECK(cmd.find("-c:v libwebp") != std::string::npos);
  CHECK(cmd.find("-q:v 55") != std::string::npos);
}

TEST_CASE("EncodeConfig suppresses ffmpeg banner and info output", "[encode-config]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mp4");

  EncodeConfig cfg;
  cfg.inputPath = inputPath;

  auto const validation = cfg.validate();
  REQUIRE(validation);

  auto const cmd = cfg.buildCMD();
  CHECK(cmd.find("-hide_banner") != std::string::npos);
  CHECK(cmd.find("-nostats") != std::string::npos);
  CHECK(cmd.find("-loglevel error") != std::string::npos);
}

TEST_CASE("EncodeConfig builds segmented mp4 command", "[encode-config]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mp4");
  auto const segOutput = temp.path / "segs" / "seg_3.ts";

  EncodeConfig cfg;
  cfg.inputPath = inputPath;
  cfg.crf = 22;
  cfg.segmentIndex = 3;
  cfg.segmentStartUs = 30'000'000;
  cfg.segmentDurationUs = 10'000'000;
  cfg.tempOutputPath = segOutput;
  cfg.progressFilePath = temp.path / "seg_3.progress";

  auto const validation = cfg.validate();
  REQUIRE(validation);

  auto const cmd = cfg.buildCMD();
  CHECK(cmd.find("-ss 30.000000") != std::string::npos);
  CHECK(cmd.find("-t 10.000000") != std::string::npos);
  CHECK(cmd.find("-force_key_frames 0") != std::string::npos);
  CHECK(cmd.find("-an") != std::string::npos);
  CHECK(cmd.find("-c:v hevc_nvenc -crf 22") != std::string::npos);
  CHECK(cmd.find("-f mpegts") != std::string::npos);
  CHECK(cmd.find(segOutput.string()) != std::string::npos);
  CHECK(
    cmd.find(std::format("{}.hevc.mp4", inputPath.stem().string())) == std::string::npos
  );
}

TEST_CASE("EncodeConfig builds audio extraction command", "[encode-config]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "sample.mp4");
  auto const audioPath = temp.path / "audio.mp4";

  auto const copyCmd =
    buildAudioExtractionCmd("ffmpeg", inputPath, audioPath, /*aacFallback=*/false);
  CHECK(copyCmd.find("-vn -c:a copy") != std::string::npos);
  CHECK(copyCmd.find("-c:v") == std::string::npos);
  CHECK(copyCmd.find(audioPath.string()) != std::string::npos);

  auto const aacCmd =
    buildAudioExtractionCmd("ffmpeg", inputPath, audioPath, /*aacFallback=*/true);
  CHECK(aacCmd.find("-vn -c:a aac -b:a 192k") != std::string::npos);
  CHECK(aacCmd.find(audioPath.string()) != std::string::npos);
}

TEST_CASE("EncodeConfig builds segment assembly command", "[encode-config]") {
  TempDir temp;
  auto const listPath = temp.path / "list.txt";
  auto const audioPath = temp.path / "audio.m4a";
  auto const outputPath = temp.path / "out.mp4";

  auto const withAudio =
    buildSegmentAssemblyCmd("ffmpeg", listPath, audioPath, outputPath);
  CHECK(withAudio.find("-f concat -safe 0") != std::string::npos);
  CHECK(withAudio.find(std::format("-i \"{}\"", listPath.string())) != std::string::npos);
  CHECK(
    withAudio.find(std::format("-i \"{}\"", audioPath.string())) != std::string::npos
  );
  CHECK(withAudio.find("-map 0:v") != std::string::npos);
  CHECK(withAudio.find("-map 1:a") != std::string::npos);
  CHECK(withAudio.find("-c copy") != std::string::npos);
  CHECK(withAudio.find(std::format("\"{}\"", outputPath.string())) != std::string::npos);

  auto const noAudio =
    buildSegmentAssemblyCmd("ffmpeg", listPath, std::nullopt, outputPath);
  CHECK(noAudio.find(std::format("-i \"{}\"", audioPath.string())) == std::string::npos);
  CHECK(noAudio.find("-map 1:a") == std::string::npos);
  CHECK(noAudio.find("-map 0:v") != std::string::npos);
  CHECK(noAudio.find(std::format("\"{}\"", outputPath.string())) != std::string::npos);
}
