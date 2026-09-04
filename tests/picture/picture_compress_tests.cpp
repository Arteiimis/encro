#include "picture/picture_compress.h"
#include "test_utils.h"

#include <filesystem>
#include <format>
#include <string>

namespace fs = std::filesystem;
using testutils::copyFakeTool;
using testutils::ScopedEnvVar;

namespace {

void configureCompressContext(
  appctx::AppContext& ctx,
  fs::path const& toolDir,
  fs::path const& inputPath
) {
  ctx.config.inputPath = inputPath;
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.toolchain.ffmpegPath = copyFakeTool(toolDir, "ffmpeg");
}

}  // namespace

TEST_CASE(
  "ImageCompressConfig::buildCMD produces valid ffmpeg command",
  "[picture-compress]"
) {
  TempDir temp;
  auto const inputPath = testutils::writeTextFile(temp.path / "photo.png");
  auto const outputPath = temp.path / "photo.jpg";

  auto const cfg = ImageCompressConfig{
    .ffmpegPath = "ffmpeg",
    .inputPath = inputPath,
    .outputPath = outputPath,
    .quality = 5,
  };

  auto const cmd = cfg.buildCMD();

  CHECK(cmd.find(" -hide_banner -nostats -loglevel error -y") != std::string::npos);
  CHECK(cmd.find(std::format(" -i \"{}\"", inputPath.string())) != std::string::npos);
  CHECK(cmd.find(" -q:v 5") != std::string::npos);
  CHECK(cmd.find(std::format(" \"{}\"", outputPath.string())) != std::string::npos);
}

TEST_CASE("ImageCompressConfig::buildCMD uses custom ffmpeg path", "[picture-compress]") {
  TempDir temp;
  auto const inputPath = testutils::writeTextFile(temp.path / "photo.png");
  auto const outputPath = temp.path / "photo.jpg";

  auto const cfg = ImageCompressConfig{
    .ffmpegPath = fs::path{"/custom/ffmpeg"},
    .inputPath = inputPath,
    .outputPath = outputPath,
    .quality = 10,
  };

  auto const cmd = cfg.buildCMD();

#if defined(_WIN32)
  CHECK(cmd.starts_with("/custom/ffmpeg "));
#else
  CHECK(cmd.starts_with("\"/custom/ffmpeg\" "));
#endif
  CHECK(cmd.find(" -q:v 10") != std::string::npos);
}

TEST_CASE("ImageCompressConfig::buildCMD uses minimum quality", "[picture-compress]") {
  TempDir temp;
  auto const inputPath = testutils::writeTextFile(temp.path / "photo.png");
  auto const outputPath = temp.path / "photo.jpg";

  auto const cfg = ImageCompressConfig{
    .inputPath = inputPath,
    .outputPath = outputPath,
    .quality = 2,
  };

  auto const cmd = cfg.buildCMD();
  CHECK(cmd.find(" -q:v 2") != std::string::npos);
}

TEST_CASE("ImageCompressConfig::buildCMD uses maximum quality", "[picture-compress]") {
  TempDir temp;
  auto const inputPath = testutils::writeTextFile(temp.path / "photo.png");
  auto const outputPath = temp.path / "photo.jpg";

  auto const cfg = ImageCompressConfig{
    .inputPath = inputPath,
    .outputPath = outputPath,
    .quality = 31,
  };

  auto const cmd = cfg.buildCMD();
  CHECK(cmd.find(" -q:v 31") != std::string::npos);
}

TEST_CASE("compressImage returns true on success", "[picture-compress]") {
  TempDir temp;
  auto const inputPath = testutils::writeTextFile(temp.path / "photo.png");
  auto const outputPath = temp.path / "photo.jpg";

  auto ctx = appctx::AppContext{};
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto const result = compressImage(ctx, inputPath, outputPath, 5);
  CHECK(result == true);
  CHECK(fs::exists(outputPath));
}

TEST_CASE("compressImage returns false on ffmpeg failure", "[picture-compress]") {
  TempDir temp;
  auto const inputPath = testutils::writeTextFile(temp.path / "photo.png");
  auto const outputPath = temp.path / "photo.jpg";

  auto const exitEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_EXIT_CODE", "1"};
  auto ctx = appctx::AppContext{};
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto const result = compressImage(ctx, inputPath, outputPath, 5);
  CHECK(result == false);
}

TEST_CASE(
  "compressImage leaves only a partial file when producer fails after writing",
  "[picture-compress]"
) {
  TempDir temp;
  auto const inputPath = testutils::writeTextFile(temp.path / "photo.png");
  auto const outputPath = temp.path / "photo.jpg";

  auto const exitEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_EXIT_CODE", "1"};
  auto const partialBytesEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_FAIL_OUTPUT_BYTES", "64"};
  auto ctx = appctx::AppContext{};
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto const result = compressImage(ctx, inputPath, outputPath, 5);
  CHECK(result == false);
  CHECK_FALSE(fs::exists(outputPath));
  CHECK(fs::exists(std::format("{}.partial", outputPath.string())));
}

TEST_CASE(
  "compressImage renames partial to final output on success",
  "[picture-compress]"
) {
  TempDir temp;
  auto const inputPath = testutils::writeTextFile(temp.path / "photo.png");
  auto const outputPath = temp.path / "photo.jpg";

  auto ctx = appctx::AppContext{};
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto const result = compressImage(ctx, inputPath, outputPath, 5);
  CHECK(result == true);
  CHECK(fs::exists(outputPath));
  CHECK_FALSE(fs::exists(std::format("{}.partial", outputPath.string())));
}

TEST_CASE("compressImageBatch compresses single image", "[picture-compress]") {
  TempDir temp;
  auto const inputPath = testutils::writeTextFile(temp.path / "photo.png");

  auto ctx = appctx::AppContext{};
  configureCompressContext(ctx, temp.path, temp.path);

  auto const tasks = std::vector<CompressTask>{
    {.inputPath = inputPath,
     .outputPath = temp.path / "photo.jpg",
     .entryName = "photo.jpg"},
  };

  auto const results = compressImageBatch(ctx, tasks, 5, 2);
  REQUIRE(results.size() == 1);
  CHECK(results[0].originalPath == inputPath);
  CHECK(results[0].compressedPath == temp.path / "photo.jpg");
  CHECK(results[0].entryName == "photo.jpg");
  CHECK(fs::exists(temp.path / "photo.jpg"));
}

TEST_CASE(
  "compressImageBatch compresses multiple images in parallel",
  "[picture-compress]"
) {
  TempDir temp;
  auto const inputA = testutils::writeTextFile(temp.path / "a.png");
  auto const inputB = testutils::writeTextFile(temp.path / "b.png");
  auto const inputC = testutils::writeTextFile(temp.path / "c.png");

  auto ctx = appctx::AppContext{};
  configureCompressContext(ctx, temp.path, temp.path);

  auto const tasks = std::vector<CompressTask>{
    {.inputPath = inputA, .outputPath = temp.path / "a.jpg", .entryName = "a.jpg"},
    {.inputPath = inputB, .outputPath = temp.path / "b.jpg", .entryName = "b.jpg"},
    {.inputPath = inputC, .outputPath = temp.path / "c.jpg", .entryName = "c.jpg"},
  };

  auto const results = compressImageBatch(ctx, tasks, 5, 3);
  REQUIRE(results.size() == 3);
  CHECK(fs::exists(temp.path / "a.jpg"));
  CHECK(fs::exists(temp.path / "b.jpg"));
  CHECK(fs::exists(temp.path / "c.jpg"));
}

TEST_CASE(
  "compressImageBatch records error for failed compression",
  "[picture-compress]"
) {
  TempDir temp;
  auto const inputPath = testutils::writeTextFile(temp.path / "photo.png");

  auto const exitEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_EXIT_CODE", "1"};
  auto ctx = appctx::AppContext{};
  configureCompressContext(ctx, temp.path, temp.path);

  auto const tasks = std::vector<CompressTask>{
    {.inputPath = inputPath,
     .outputPath = temp.path / "photo.jpg",
     .entryName = "photo.jpg"},
  };

  auto const results = compressImageBatch(ctx, tasks, 5, 1);
  CHECK(results.empty());
}
