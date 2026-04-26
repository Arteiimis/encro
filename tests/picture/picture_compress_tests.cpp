#include "picture/picture_compress.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

auto createTempFile(fs::path const& dir, std::string_view name) -> fs::path {
  auto const filePath = dir / name;
  auto out = std::ofstream{filePath, std::ios::binary};
  out << "dummy";
  return filePath;
}

#if defined(_WIN32)
auto makeCmdScriptCommand(fs::path const& scriptPath) -> fs::path {
  return fs::path{std::format("cmd.exe /d /c call \"{}\"", scriptPath.string())};
}

void writeFakeFfmpegScript(fs::path const& scriptPath) {
  auto const script = std::string{R"(@echo off
set "outputPath="
:parse
if "%~1"=="" goto done
set "outputPath=%~1"
shift
goto parse
:done
if "%outputPath%"=="" exit /b 2
for %%I in ("%outputPath%") do if not "%%~dpI"=="" mkdir "%%~dpI" 2>nul
>"%outputPath%" echo fake-compressed-jpeg
exit /b 0
)"};
  testutils::writeTextFile(scriptPath, script);
}

void writeFakeFfmpegFailingScript(fs::path const& scriptPath) {
  auto const script = std::string{R"(@echo off
exit /b 1
)"};
  testutils::writeTextFile(scriptPath, script);
}

void configureCompressContext(
  appctx::AppContext& ctx,
  fs::path const& ffmpegScriptPath,
  fs::path const& inputPath
) {
  ctx.config.inputPath = inputPath;
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.verboseEcho = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(ffmpegScriptPath);
}
#endif

}  // namespace

TEST_CASE(
  "ImageCompressConfig::buildCMD produces valid ffmpeg command",
  "[picture-compress]"
) {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "photo.png");
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
  auto const inputPath = createTempFile(temp.path, "photo.png");
  auto const outputPath = temp.path / "photo.jpg";

  auto const cfg = ImageCompressConfig{
    .ffmpegPath = fs::path{"/custom/ffmpeg"},
    .inputPath = inputPath,
    .outputPath = outputPath,
    .quality = 10,
  };

  auto const cmd = cfg.buildCMD();

  CHECK(cmd.starts_with("/custom/ffmpeg "));
  CHECK(cmd.find(" -q:v 10") != std::string::npos);
}

TEST_CASE("ImageCompressConfig::buildCMD uses minimum quality", "[picture-compress]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "photo.png");
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
  auto const inputPath = createTempFile(temp.path, "photo.png");
  auto const outputPath = temp.path / "photo.jpg";

  auto const cfg = ImageCompressConfig{
    .inputPath = inputPath,
    .outputPath = outputPath,
    .quality = 31,
  };

  auto const cmd = cfg.buildCMD();
  CHECK(cmd.find(" -q:v 31") != std::string::npos);
}

#if defined(_WIN32)
TEST_CASE("compressImage returns true on success", "[picture-compress]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "photo.png");
  auto const outputPath = temp.path / "photo.jpg";
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  writeFakeFfmpegScript(scriptPath);

  auto ctx = appctx::AppContext{};
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto const result = compressImage(ctx, inputPath, outputPath, 5);
  CHECK(result == true);
  CHECK(fs::exists(outputPath));
}

TEST_CASE("compressImage returns false on ffmpeg failure", "[picture-compress]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "photo.png");
  auto const outputPath = temp.path / "photo.jpg";
  auto const scriptPath = temp.path / "fake_ffmpeg_fail.cmd";
  writeFakeFfmpegFailingScript(scriptPath);

  auto ctx = appctx::AppContext{};
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto const result = compressImage(ctx, inputPath, outputPath, 5);
  CHECK(result == false);
}

TEST_CASE("compressImageBatch returns empty for empty input", "[picture-compress]") {
  TempDir temp;
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  writeFakeFfmpegScript(scriptPath);

  auto ctx = appctx::AppContext{};
  configureCompressContext(ctx, scriptPath, temp.path);

  auto const results = compressImageBatch(ctx, std::span<CompressTask const>{}, 5, 2);
  CHECK(results.empty());
}

TEST_CASE("compressImageBatch compresses single image", "[picture-compress]") {
  TempDir temp;
  auto const inputPath = createTempFile(temp.path, "photo.png");
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  writeFakeFfmpegScript(scriptPath);

  auto ctx = appctx::AppContext{};
  configureCompressContext(ctx, scriptPath, temp.path);

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
  auto const inputA = createTempFile(temp.path, "a.png");
  auto const inputB = createTempFile(temp.path, "b.png");
  auto const inputC = createTempFile(temp.path, "c.png");
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  writeFakeFfmpegScript(scriptPath);

  auto ctx = appctx::AppContext{};
  configureCompressContext(ctx, scriptPath, temp.path);

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
  auto const inputPath = createTempFile(temp.path, "photo.png");
  auto const scriptPath = temp.path / "fake_ffmpeg_fail.cmd";
  writeFakeFfmpegFailingScript(scriptPath);

  auto ctx = appctx::AppContext{};
  configureCompressContext(ctx, scriptPath, temp.path);

  auto const tasks = std::vector<CompressTask>{
    {.inputPath = inputPath,
     .outputPath = temp.path / "photo.jpg",
     .entryName = "photo.jpg"},
  };

  auto const results = compressImageBatch(ctx, tasks, 5, 1);
  CHECK(results.empty());
}

TEST_CASE(
  "compressImageBatch returns success results alongside failures",
  "[picture-compress]"
) {
  TempDir temp;
  auto const inputOk = createTempFile(temp.path, "ok.png");
  auto const inputFail = createTempFile(temp.path, "fail.png");
  auto const goodScript = temp.path / "fake_good.cmd";
  auto const failScript = temp.path / "fake_fail.cmd";
  writeFakeFfmpegScript(goodScript);
  writeFakeFfmpegFailingScript(failScript);

  auto ctx = appctx::AppContext{};
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.verboseEcho = true;

  auto const tasks = std::vector<CompressTask>{
    {
      .inputPath = inputOk,
      .outputPath = temp.path / "ok.jpg",
      .entryName = "ok.jpg",
    },
    {
      .inputPath = inputFail,
      .outputPath = temp.path / "fail.jpg",
      .entryName = "fail.jpg",
    },
  };

  auto results = std::vector<CompressResult>{};
  results.reserve(2);

  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(goodScript);
  {
    auto const taskOk = std::vector<CompressTask>{tasks[0]};
    auto const batchOk = compressImageBatch(ctx, taskOk, 5, 1);
    for (auto const& r: batchOk) { results.push_back(r); }
  }

  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(failScript);
  {
    auto const taskFail = std::vector<CompressTask>{tasks[1]};
    auto const batchFail = compressImageBatch(ctx, taskFail, 5, 1);
    for (auto const& r: batchFail) { results.push_back(r); }
  }

  REQUIRE(results.size() == 1);
  CHECK(results[0].entryName == "ok.jpg");
}
#endif
