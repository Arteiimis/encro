#include "encode_config.h"
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
    validation.error().find("CRF value must be between 0 and 51")
    != std::string::npos
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

TEST_CASE("EncodeConfig uses codec prefix in non-webp output filename", "[encode-config]") {
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

TEST_CASE("EncodeConfig webp output filename does not include codec tag", "[encode-config]") {
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
