#include "picture/picture_process.h"
#include "picture/picture_compress.h"
#include "pack/pack.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

auto createSparseSizedFile(
  fs::path const& dir,
  std::string_view name,
  std::uintmax_t sizeBytes
) -> fs::path {
  auto const filePath = dir / name;
  auto out = std::ofstream{filePath, std::ios::binary};
  REQUIRE(out.is_open());

  if (sizeBytes > 0) {
    out.seekp(static_cast<std::streamoff>(sizeBytes - 1));
    out.put('\0');
  }

  out.close();
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

auto writeFakeFfmpegFailingScript(fs::path const& scriptPath) -> void {
  testutils::writeTextFile(scriptPath, "@echo off\nexit /b 1\n");
}
#endif

}  // namespace

TEST_CASE(
  "execute() Media mode produces subPart split for size overflow",
  "[picture-process][pack]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const outputDir = temp.path / "packed";
  fs::create_directories(inputDir);

  constexpr auto kSize = std::uintmax_t{240ULL * 1024ULL * 1024ULL};
  auto const f1 = createSparseSizedFile(inputDir, "a.jpg", kSize);
  auto const f2 = createSparseSizedFile(inputDir, "b.jpg", kSize);
  auto const f3 = createSparseSizedFile(inputDir, "c.jpg", kSize);

  auto const result = pack::execute(
    pack::PackRequest{
      .entries = {f1, f2, f3},
      .mode = pack::PackMode::Media,
      .outputDir = outputDir,
      .compact = true,
      .removeOnFailure = true,
    }
  );

  REQUIRE(result.has_value());
  REQUIRE(result->exitCode == 0);

  // 3 * 240MB = 720MB > 500MB limit -> subPart split expected
  CHECK(result->zippedFiles.size() >= 2);

  for (auto const& f: result->zippedFiles) {
    CHECK(fs::exists(f));
    CHECK(fs::file_size(f) > 0);
  }
}

TEST_CASE(
  "execute() Media mode with naming produces baseName prefixed zip names",
  "[picture-process][pack]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const outputDir = temp.path / "packed";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);

  auto const a1 = createSparseSizedFile(dirA, "alpha.jpg", 32);
  auto const a2 = createSparseSizedFile(dirA, "beta.jpg", 32);
  auto const b1 = createSparseSizedFile(dirB, "alpha.jpg", 32);
  auto const b2 = createSparseSizedFile(dirB, "beta.jpg", 32);

  auto const result = pack::execute(
    pack::PackRequest{
      .entries = {a1, a2, b1, b2},
      .mode = pack::PackMode::Media,
      .outputDir = outputDir,
      .compact = true,
      .removeOnFailure = true,
      .naming = pack::NamingConfig{
        .layout = appctx::OutputLayout::Flat,
        .baseName = "pics",
      },
    }
  );

  REQUIRE(result.has_value());
  REQUIRE(result->exitCode == 0);

  // 4 small files fit in 1 zip
  REQUIRE(result->zippedFiles.size() == 1);

  auto const zipName = result->zippedFiles[0].filename().string();
  CHECK(zipName.find("pics_part1") != std::string::npos);
  CHECK(fs::exists(result->zippedFiles[0]));
  CHECK(fs::file_size(result->zippedFiles[0]) > 0);
}

TEST_CASE("runPicturePackWorkflow packs directory", "[picture-process]") {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  fs::create_directories(inputDir);
  createSparseSizedFile(inputDir, "a.jpg", 32);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.inputPath = inputDir;

  auto const runRes = runPicturePackWorkflow(ctx, inputDir);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK(fs::exists(inputDir / "packed" / "pics_part1[1~1#1p].zip"));
}

#if defined(_WIN32)
TEST_CASE(
  "runPicturePackWorkflow compress+pack produces .jpg entries in zip",
  "[picture-process][compress]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  fs::create_directories(inputDir);
  createSparseSizedFile(inputDir, "a.png", 32);
  createSparseSizedFile(inputDir, "b.png", 32);
  writeFakeFfmpegScript(scriptPath);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.verboseEcho = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto const runRes = runPicturePackWorkflow(ctx, inputDir);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto const entryNames =
    testutils::listZipRegularEntryNames(inputDir / "packed" / "part1[1~2#2p].zip");
  REQUIRE(entryNames.size() == 2);
  CHECK(entryNames[0].ends_with(".jpg"));
  CHECK(entryNames[1].ends_with(".jpg"));
}

TEST_CASE(
  "runPicturePackWorkflow compress preserves collision-safe names",
  "[picture-process][compress]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  createSparseSizedFile(dirA, "same.png", 32);
  createSparseSizedFile(dirB, "same.png", 32);
  writeFakeFfmpegScript(scriptPath);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.verbose = true;
  ctx.config.verboseEcho = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto const runRes = runPicturePackWorkflow(ctx, inputDir);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto const entryNames =
    testutils::listZipRegularEntryNames(inputDir / "packed" / "part1[1~2#2p].zip");
  REQUIRE(entryNames.size() == 2);
  CHECK(entryNames[0].ends_with(".jpg"));
  CHECK(entryNames[1].ends_with(".jpg"));
  CHECK(entryNames[0] != entryNames[1]);
  CHECK(entryNames[0].starts_with("1000__"));
  CHECK(entryNames[1].starts_with("1000__"));
  CHECK(
    (testutils::hasCollisionSafePrefix(entryNames[0], "a", "same")
     || testutils::hasCollisionSafePrefix(entryNames[0], "b", "same"))
  );
  CHECK(
    (testutils::hasCollisionSafePrefix(entryNames[1], "a", "same")
     || testutils::hasCollisionSafePrefix(entryNames[1], "b", "same"))
  );
}

TEST_CASE(
  "runPicturePackWorkflow compress with folder-summary adds summary jpg entries",
  "[picture-process][compress]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  createSparseSizedFile(dirA, "alpha.png", 32);
  createSparseSizedFile(dirA, "beta.png", 32);
  createSparseSizedFile(dirB, "alpha.png", 32);
  createSparseSizedFile(dirB, "beta.png", 32);
  writeFakeFfmpegScript(scriptPath);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.verbose = true;
  ctx.config.verboseEcho = true;
  ctx.config.pictureFolderSummary = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto const runRes = runPicturePackWorkflow(ctx, inputDir);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto const entryNames =
    testutils::listZipRegularEntryNames(inputDir / "packed" / "part1[1~6#6p].zip");
  REQUIRE(entryNames.size() >= 4);

  for (auto const& name: entryNames) { CHECK(name.ends_with(".jpg")); }
  CHECK(entryNames[0].starts_with("0000__summary__"));
  CHECK(entryNames[1].starts_with("0000__summary__"));
  CHECK(entryNames[2].starts_with("1000__"));
  CHECK(entryNames[3].starts_with("1000__"));
  CHECK(entryNames[4].starts_with("1000__"));
  CHECK(entryNames[5].starts_with("1000__"));
}

TEST_CASE(
  "runPicturePackWorkflow compress cancels when user declines",
  "[picture-process][compress]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  fs::create_directories(inputDir);
  createSparseSizedFile(inputDir, "a.png", 32);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = false;
  ctx.config.verbose = true;
  ctx.config.verboseEcho = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.inputPath = inputDir;

  auto const runRes = runPicturePackWorkflow(ctx, inputDir);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK_FALSE(fs::exists(inputDir / "packed"));
}

TEST_CASE(
  "runPicturePackWorkflow compress returns error when all compressions fail",
  "[picture-process][compress]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const scriptPath = temp.path / "fake_ffmpeg_fail.cmd";
  fs::create_directories(inputDir);
  createSparseSizedFile(inputDir, "a.png", 32);
  writeFakeFfmpegFailingScript(scriptPath);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.verboseEcho = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto const runRes = runPicturePackWorkflow(ctx, inputDir);
  REQUIRE(!runRes);
  CHECK(runRes.error() == "All picture compressions failed.");
}

TEST_CASE(
  "addCompressTask deduplicates and creates valid CompressTask entries",
  "[picture-process][compress]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  fs::create_directories(inputDir);
  createSparseSizedFile(inputDir, "photo.png", 32);
  createSparseSizedFile(inputDir, "other.png", 32);
  writeFakeFfmpegScript(scriptPath);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.verboseEcho = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto const runRes = runPicturePackWorkflow(ctx, inputDir);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto const entryNames =
    testutils::listZipRegularEntryNames(inputDir / "packed" / "part1[1~2#2p].zip");
  REQUIRE(entryNames.size() == 2);
  CHECK(entryNames[0].ends_with(".jpg"));
  CHECK(entryNames[1].ends_with(".jpg"));
  CHECK(entryNames[0] != entryNames[1]);
}
#endif
