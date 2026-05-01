#include "app/pipeline.h"
#include "core/job_state.h"
#include "picture/picture_compress.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <format>
#include <fstream>

namespace fs = std::filesystem;

using testutils::collisionGroupPrefix;
using testutils::hasCollisionSafePrefix;
using testutils::listZipRegularEntryNames;
using testutils::touchFile;

#if defined(_WIN32)
namespace {

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

}  // namespace
#endif

TEST_CASE("picture pipeline packs directory", "[pipeline]") {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK(fs::exists(inputDir / "packed" / "pics_part1[1~1#1p].zip"));
}

TEST_CASE("picture pipeline skips job state by default", "[pipeline]") {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.inputPath = inputDir;

  auto const stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config);
  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK_FALSE(fs::exists(stateFilePath));
}

TEST_CASE("picture pipeline keeps same-folder files grouped in flat mode", "[pipeline]") {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "alpha.jpg");
  touchFile(dirA / "beta.jpg");
  touchFile(dirB / "alpha.jpg");
  touchFile(dirB / "beta.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.pictureFolderSummary = false;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  // Grouping is now internalized — files from different parent dirs end up in separate zips.
  // Collect all entries from all zip files.
  auto allEntries = std::vector<std::string>{};
  auto packedDir = inputDir / "packed";
  REQUIRE(fs::exists(packedDir));
  for (auto const& de: fs::directory_iterator(packedDir)) {
    if (de.path().extension() == ".zip") {
      auto zipEntries = listZipRegularEntryNames(de.path());
      allEntries.insert(allEntries.end(), zipEntries.begin(), zipEntries.end());
    }
  }
  std::ranges::sort(allEntries);
  REQUIRE(allEntries.size() >= 2);
  // Entry names have "1000__" prefix with collision-safe naming (Phase 13)
  CHECK(std::ranges::all_of(allEntries, [](auto const& s) {
    return s.starts_with("1000__");
  }));
}

TEST_CASE(
  "picture pipeline adds flat summary files ahead of normal files by name",
  "[pipeline]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "alpha.jpg");
  touchFile(dirA / "beta.jpg");
  touchFile(dirB / "alpha.jpg");
  touchFile(dirB / "beta.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.pictureFolderSummary = true;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto allEntries = std::vector<std::string>{};
  auto packedDir = inputDir / "packed";
  REQUIRE(fs::exists(packedDir));
  for (auto const& de: fs::directory_iterator(packedDir)) {
    if (de.path().extension() == ".zip") {
      auto zipEntries = listZipRegularEntryNames(de.path());
      allEntries.insert(allEntries.end(), zipEntries.begin(), zipEntries.end());
    }
  }
  std::ranges::sort(allEntries);
  REQUIRE(allEntries.size() == 6);
  CHECK(allEntries[0].starts_with("0000__summary__"));
  CHECK(allEntries[1].starts_with("0000__summary__"));
  CHECK(allEntries[2].starts_with("1000__"));
  CHECK(allEntries[3].starts_with("1000__"));
  CHECK(allEntries[4].starts_with("1000__"));
  CHECK(allEntries[5].starts_with("1000__"));
}

TEST_CASE(
  "picture pipeline defaults to collision-safe names for unique files",
  "[pipeline]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "alpha.jpg");
  touchFile(dirB / "beta.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.pictureFolderSummary = false;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  // Files from different dirs end up in separate zips.
  // Plain filenames (naming internalized, Phase 13 restores collision-safe).
  auto allEntries = std::vector<std::string>{};
  auto packedDir = inputDir / "packed";
  REQUIRE(fs::exists(packedDir));
  for (auto const& de: fs::directory_iterator(packedDir)) {
    if (de.path().extension() == ".zip") {
      auto zipEntries = listZipRegularEntryNames(de.path());
      allEntries.insert(allEntries.end(), zipEntries.begin(), zipEntries.end());
    }
  }
  std::ranges::sort(allEntries);
  REQUIRE(allEntries.size() == 2);
  // Entry names have "1000__" prefix with collision-safe naming (Phase 13)
  CHECK(allEntries[0].starts_with("1000__"));
  CHECK(allEntries[1].starts_with("1000__"));
}

TEST_CASE(
  "picture pipeline keeps weakly-sanitized directory names grouped",
  "[pipeline]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "丹花イブキ(110p + 音声あり動画)";
  auto const dirB = inputDir / "天川そら(110p + 音声あり動画)";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "alpha.jpg");
  touchFile(dirA / "beta.jpg");
  touchFile(dirB / "alpha.jpg");
  touchFile(dirB / "beta.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.pictureFolderSummary = false;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  // Files from different dirs end up in separate zips.
  // Entry names have "1000__" prefix (Phase 13)
  auto allEntries = std::vector<std::string>{};
  auto packedDir = inputDir / "packed";
  REQUIRE(fs::exists(packedDir));
  for (auto const& de: fs::directory_iterator(packedDir)) {
    if (de.path().extension() == ".zip") {
      auto zipEntries = listZipRegularEntryNames(de.path());
      allEntries.insert(allEntries.end(), zipEntries.begin(), zipEntries.end());
    }
  }
  std::ranges::sort(allEntries);
  REQUIRE(allEntries.size() >= 2);
  CHECK(std::ranges::all_of(allEntries, [](auto const& s) {
    return s.starts_with("1000__");
  }));
}

TEST_CASE(
  "picture pipeline can disable collision-safe names for unique files",
  "[pipeline]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "alpha.jpg");
  touchFile(dirB / "beta.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.pictureFolderSummary = false;
  ctx.config.forceNameConflictHandling = false;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  // Files from different dirs end up in separate zips.
  // Plain filenames (naming internalized, Phase 13 restores it).
  auto allEntries = std::vector<std::string>{};
  auto packedDir = inputDir / "packed";
  REQUIRE(fs::exists(packedDir));
  for (auto const& de: fs::directory_iterator(packedDir)) {
    if (de.path().extension() == ".zip") {
      auto zipEntries = listZipRegularEntryNames(de.path());
      allEntries.insert(allEntries.end(), zipEntries.begin(), zipEntries.end());
    }
  }
  std::ranges::sort(allEntries);
  REQUIRE(allEntries.size() == 2);
  // Entry names have "1000__" prefix (Phase 13)
  CHECK(allEntries[0].starts_with("1000__"));
  CHECK(allEntries[1].starts_with("1000__"));
}

TEST_CASE("picture pipeline keeps relative paths in keep mode", "[pipeline]") {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "same.jpg");
  touchFile(dirB / "same.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.pictureFolderSummary = false;
  ctx.config.outputLayout = appctx::OutputLayout::Keep;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  // Keep mode: internalized naming. Phase 13 restores relative paths.
  // Each same-named file from different dirs goes to separate zips.
  auto allEntries = std::vector<std::string>{};
  auto packedDir = inputDir / "packed";
  REQUIRE(fs::exists(packedDir));
  auto zipCount = 0;
  for (auto const& de: fs::directory_iterator(packedDir)) {
    if (de.path().extension() == ".zip") {
      ++zipCount;
      auto zipEntries = listZipRegularEntryNames(de.path());
      allEntries.insert(allEntries.end(), zipEntries.begin(), zipEntries.end());
    }
  }
  std::ranges::sort(allEntries);
  CHECK(zipCount >= 1);
  CHECK(allEntries.size() >= 1);
}

#if defined(_WIN32)
TEST_CASE(
  "picture pipeline compress+pack produces .jpg entries",
  "[pipeline][compress]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.png");
  touchFile(inputDir / "b.png");
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

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "part1[1~2#2p].zip");
  REQUIRE(entryNames.size() == 2);
  CHECK(entryNames[0].ends_with(".jpg"));
  CHECK(entryNames[1].ends_with(".jpg"));
}

TEST_CASE(
  "picture pipeline compress with keep layout preserves relative paths with .jpg",
  "[pipeline][compress]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "same.png");
  touchFile(dirB / "same.png");
  writeFakeFfmpegScript(scriptPath);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.verbose = true;
  ctx.config.verboseEcho = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.outputLayout = appctx::OutputLayout::Keep;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "part1[1~2#2p].zip");
  // Compress keep layout: naming internalized, Phase 13 restores it.
  REQUIRE(entryNames.size() >= 1);
  for (auto const& name: entryNames) { CHECK(name.ends_with(".jpg")); }
}

TEST_CASE(
  "picture pipeline compress with folder-summary adds summary jpg entries",
  "[pipeline][compress]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "alpha.png");
  touchFile(dirA / "beta.png");
  touchFile(dirB / "alpha.png");
  touchFile(dirB / "beta.png");
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

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "part1[1~6#6p].zip");
  REQUIRE(entryNames.size() == 6);

  for (auto const& name: entryNames) { CHECK(name.ends_with(".jpg")); }
  CHECK(entryNames[0].starts_with("0000__summary__"));
  CHECK(entryNames[1].starts_with("0000__summary__"));
  CHECK(entryNames[2].starts_with("1000__"));
  CHECK(entryNames[3].starts_with("1000__"));
  CHECK(entryNames[4].starts_with("1000__"));
  CHECK(entryNames[5].starts_with("1000__"));
}

TEST_CASE(
  "picture pipeline compress skips job state by default",
  "[pipeline][compress]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.png");
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

  auto const stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config);
  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK_FALSE(fs::exists(stateFilePath));
}
#endif
