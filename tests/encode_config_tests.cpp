#include "encode_config.h"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

struct TempDir {
  fs::path path;

  TempDir() {
    auto const base = fs::temp_directory_path();
    path = base
         / std::format(
             "video_encoder_tests_{}",
             std::chrono::steady_clock::now().time_since_epoch().count()
         );
    fs::create_directories(path);
  }

  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

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
