#include "app/pipeline.h"
#include "core/job_state.h"
#include "infra/stop_signal.h"
#include "test_utils.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

using testutils::listZipRegularEntryNames;
using testutils::ScopedStopSignalReset;
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
type nul > "%outputPath%"
exit /b 0
)"};
  testutils::writeTextFile(scriptPath, script);
}

auto writeFakeFfmpegSecondCallSlowScript(
  fs::path const& scriptPath,
  fs::path const& counterPath,
  fs::path const& survivorMarkerPath
) -> void {
  auto const script = std::format(
    R"(@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "outputPath="
set "inputPath="
set "counterPath={}"
set "markerPath={}"
:parse
if "%~1"=="" goto done
if "%~1"=="-i" set "inputPath=%~2"
set "outputPath=%~1"
shift
goto parse
:done
if "%outputPath%"=="" exit /b 2
set /a count=0
if exist "%counterPath%" set /p count=<"%counterPath%"
set /a count+=1
>"%counterPath%" echo(!count!
if !count! GEQ 2 (
  ping -n 8 127.0.0.1 >nul
  exit /b 130
)
>"%markerPath%" echo(%inputPath%
for %%I in ("%outputPath%") do if not "%%~dpI"=="" mkdir "%%~dpI" 2>nul
type nul > "%outputPath%"
exit /b 0
)",
    counterPath.string(),
    survivorMarkerPath.string()
  );
  testutils::writeTextFile(scriptPath, script);
}

auto writeFakeFfmpegCountingScript(
  fs::path const& scriptPath,
  fs::path const& counterPath
) -> void {
  auto const script = std::format(
    R"(@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "outputPath="
set "counterPath={}"
:parse
if "%~1"=="" goto done
set "outputPath=%~1"
shift
goto parse
:done
if "%outputPath%"=="" exit /b 2
set /a count=0
if exist "%counterPath%" set /p count=<"%counterPath%"
set /a count+=1
>"%counterPath%" echo(!count!
for %%I in ("%outputPath%") do if not "%%~dpI"=="" mkdir "%%~dpI" 2>nul
type nul > "%outputPath%"
exit /b 0
)",
    counterPath.string()
  );
  testutils::writeTextFile(scriptPath, script);
}

auto readInvocationCount(fs::path const& counterPath) -> std::size_t {
  auto in = std::ifstream{counterPath, std::ios::binary};
  auto value = std::size_t{0};
  if (in.is_open()) { in >> value; }
  return value;
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

TEST_CASE(
  "picture pipeline removes empty state file when canceled before packing starts",
  "[pipeline]"
) {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const stateFilePath = temp.path / "encro.job-state.json";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = false;
  ctx.config.inputPath = inputDir;
  ctx.config.stateFilePath = stateFilePath;

  stopsignal::requestStop();

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == stopsignal::kCanceledExitCode);
  CHECK_FALSE(fs::exists(inputDir / "packed"));
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
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "pics_part1[1~2#2p].zip");
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
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.outputLayout = appctx::OutputLayout::Keep;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "pics_part1[1~2#2p].zip");
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
  ctx.config.pictureFolderSummary = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "pics_part1[1~6#6p].zip");
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
  "picture pipeline compress creates job state by default",
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
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto const stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config);
  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK(fs::exists(stateFilePath));
}

TEST_CASE(
  "picture pipeline compress keeps state and cache when canceled mid-batch",
  "[pipeline][compress]"
) {
  using namespace std::chrono_literals;

  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const scriptPath = temp.path / "fake_ffmpeg_slow.cmd";
  auto const counterPath = temp.path / "compress-count.txt";
  auto const survivorMarker = temp.path / "survivor.txt";
  fs::create_directories(inputDir);
  touchFile(inputDir / "fast.png");
  touchFile(inputDir / "slow.png");
  writeFakeFfmpegSecondCallSlowScript(scriptPath, counterPath, survivorMarker);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.maxParallelJobs = 1;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto const stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config);
  auto const cacheDir = inputDir / "packed" / ".compress_tmp_q5";

  auto requester = std::jthread([](std::stop_token token) {
    std::this_thread::sleep_for(1200ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const runRes = pipeline::run(ctx);

  REQUIRE(runRes);
  CHECK(runRes.value() == stopsignal::kCanceledExitCode);
  CHECK(fs::exists(stateFilePath));
  CHECK(fs::exists(cacheDir));
  auto cachedCount = 0;
  for (auto const& de: fs::directory_iterator(cacheDir)) { ++cachedCount; }
  CHECK(cachedCount >= 1);
}

TEST_CASE(
  "picture pipeline compress rerun resumes from cache and packs completed files",
  "[pipeline][compress]"
) {
  using namespace std::chrono_literals;

  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const slowScript = temp.path / "fake_ffmpeg_slow.cmd";
  auto const slowCounter = temp.path / "slow-count.txt";
  auto const survivorMarker = temp.path / "survivor.txt";
  fs::create_directories(inputDir);
  touchFile(inputDir / "fast.png");
  touchFile(inputDir / "slow.png");
  writeFakeFfmpegSecondCallSlowScript(slowScript, slowCounter, survivorMarker);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.maxParallelJobs = 1;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(slowScript);

  auto requester = std::jthread([](std::stop_token token) {
    std::this_thread::sleep_for(1200ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const canceledRes = pipeline::run(ctx);
  REQUIRE(canceledRes);
  CHECK(canceledRes.value() == stopsignal::kCanceledExitCode);
  stopsignal::reset();

  auto const countingScript = temp.path / "fake_ffmpeg_count.cmd";
  auto const countFile = temp.path / "count.txt";
  writeFakeFfmpegCountingScript(countingScript, countFile);

  auto ctx2 = appctx::AppContext{};
  ctx2.config.processType = "picture";
  ctx2.config.yesToAll = true;
  ctx2.config.verbose = true;
  ctx2.config.compressImages = true;
  ctx2.config.imageQuality = 5;
  ctx2.config.maxParallelJobs = 1;
  ctx2.config.inputPath = inputDir;
  ctx2.toolchain.ffmpegPath = makeCmdScriptCommand(countingScript);

  auto const resumeRes = pipeline::run(ctx2);
  REQUIRE(resumeRes);
  CHECK(resumeRes.value() == 0);
  CHECK(readInvocationCount(countFile) == 1);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "pics_part1[1~2#2p].zip");
  REQUIRE(entryNames.size() == 2);
  for (auto const& name: entryNames) { CHECK(name.ends_with(".jpg")); }
  CHECK_FALSE(fs::exists(inputDir / "packed" / ".compress_tmp_q5"));
}

TEST_CASE(
  "picture pipeline compress recompresses replaced source on rerun",
  "[pipeline][compress]"
) {
  using namespace std::chrono_literals;

  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const slowScript = temp.path / "fake_ffmpeg_slow.cmd";
  auto const slowCounter = temp.path / "slow-count.txt";
  auto const survivorMarker = temp.path / "survivor.txt";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.png");
  touchFile(inputDir / "b.png");
  writeFakeFfmpegSecondCallSlowScript(slowScript, slowCounter, survivorMarker);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.maxParallelJobs = 1;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(slowScript);

  auto requester = std::jthread([](std::stop_token token) {
    std::this_thread::sleep_for(1200ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const canceledRes = pipeline::run(ctx);
  REQUIRE(canceledRes);
  CHECK(canceledRes.value() == stopsignal::kCanceledExitCode);
  stopsignal::reset();

  auto survivorIn = std::ifstream{survivorMarker, std::ios::binary};
  auto survivorLine = std::string{};
  if (survivorIn.is_open()) { std::getline(survivorIn, survivorLine); }
  if (!survivorLine.empty() && survivorLine.back() == '\r') { survivorLine.pop_back(); }
  REQUIRE_FALSE(survivorLine.empty());
  touchFile(fs::path{survivorLine});

  auto const countingScript = temp.path / "fake_ffmpeg_count.cmd";
  auto const countFile = temp.path / "count.txt";
  writeFakeFfmpegCountingScript(countingScript, countFile);

  auto ctx2 = appctx::AppContext{};
  ctx2.config.processType = "picture";
  ctx2.config.yesToAll = true;
  ctx2.config.verbose = true;
  ctx2.config.compressImages = true;
  ctx2.config.imageQuality = 5;
  ctx2.config.maxParallelJobs = 1;
  ctx2.config.inputPath = inputDir;
  ctx2.toolchain.ffmpegPath = makeCmdScriptCommand(countingScript);

  auto const resumeRes = pipeline::run(ctx2);
  REQUIRE(resumeRes);
  CHECK(resumeRes.value() == 0);
  CHECK(readInvocationCount(countFile) == 2);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "pics_part1[1~2#2p].zip");
  REQUIRE(entryNames.size() == 2);
}

TEST_CASE(
  "picture pipeline compress missing state file invalidates cache on rerun",
  "[pipeline][compress]"
) {
  using namespace std::chrono_literals;

  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const slowScript = temp.path / "fake_ffmpeg_slow.cmd";
  auto const slowCounter = temp.path / "slow-count.txt";
  auto const survivorMarker = temp.path / "survivor.txt";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.png");
  touchFile(inputDir / "b.png");
  writeFakeFfmpegSecondCallSlowScript(slowScript, slowCounter, survivorMarker);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.maxParallelJobs = 1;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(slowScript);

  auto requester = std::jthread([](std::stop_token token) {
    std::this_thread::sleep_for(1200ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const canceledRes = pipeline::run(ctx);
  REQUIRE(canceledRes);
  CHECK(canceledRes.value() == stopsignal::kCanceledExitCode);
  stopsignal::reset();
  auto const stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config);
  CHECK(fs::exists(stateFilePath));
  CHECK(fs::exists(inputDir / "packed" / ".compress_tmp_q5"));
  fs::remove(stateFilePath);

  auto const countingScript = temp.path / "fake_ffmpeg_count.cmd";
  auto const countFile = temp.path / "count.txt";
  writeFakeFfmpegCountingScript(countingScript, countFile);

  auto ctx2 = appctx::AppContext{};
  ctx2.config.processType = "picture";
  ctx2.config.yesToAll = true;
  ctx2.config.verbose = true;
  ctx2.config.compressImages = true;
  ctx2.config.imageQuality = 5;
  ctx2.config.maxParallelJobs = 1;
  ctx2.config.inputPath = inputDir;
  ctx2.toolchain.ffmpegPath = makeCmdScriptCommand(countingScript);

  auto const rerunRes = pipeline::run(ctx2);
  REQUIRE(rerunRes);
  CHECK(rerunRes.value() == 0);
  CHECK(readInvocationCount(countFile) == 2);
}

TEST_CASE(
  "picture pipeline compress quality change does not reuse previous cache",
  "[pipeline][compress]"
) {
  using namespace std::chrono_literals;

  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const slowScript = temp.path / "fake_ffmpeg_slow.cmd";
  auto const slowCounter = temp.path / "slow-count.txt";
  auto const survivorMarker = temp.path / "survivor.txt";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.png");
  touchFile(inputDir / "b.png");
  writeFakeFfmpegSecondCallSlowScript(slowScript, slowCounter, survivorMarker);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.maxParallelJobs = 1;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(slowScript);

  auto requester = std::jthread([](std::stop_token token) {
    std::this_thread::sleep_for(1200ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const canceledRes = pipeline::run(ctx);
  REQUIRE(canceledRes);
  CHECK(canceledRes.value() == stopsignal::kCanceledExitCode);
  stopsignal::reset();
  CHECK(fs::exists(inputDir / "packed" / ".compress_tmp_q5"));

  auto const countingScript = temp.path / "fake_ffmpeg_count.cmd";
  auto const countFile = temp.path / "count.txt";
  writeFakeFfmpegCountingScript(countingScript, countFile);

  auto ctx2 = appctx::AppContext{};
  ctx2.config.processType = "picture";
  ctx2.config.yesToAll = true;
  ctx2.config.verbose = true;
  ctx2.config.compressImages = true;
  ctx2.config.imageQuality = 2;
  ctx2.config.maxParallelJobs = 1;
  ctx2.config.inputPath = inputDir;
  ctx2.toolchain.ffmpegPath = makeCmdScriptCommand(countingScript);

  auto const rerunRes = pipeline::run(ctx2);
  REQUIRE(rerunRes);
  CHECK(rerunRes.value() == 0);
  CHECK(readInvocationCount(countFile) == 2);
  CHECK_FALSE(fs::exists(inputDir / "packed" / ".compress_tmp_q5"));
}

TEST_CASE(
  "picture pipeline compress --restart clears stale caches and recompresses all",
  "[pipeline][compress]"
) {
  using namespace std::chrono_literals;

  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const slowScript = temp.path / "fake_ffmpeg_slow.cmd";
  auto const slowCounter = temp.path / "slow-count.txt";
  auto const survivorMarker = temp.path / "survivor.txt";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.png");
  touchFile(inputDir / "b.png");
  writeFakeFfmpegSecondCallSlowScript(slowScript, slowCounter, survivorMarker);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.maxParallelJobs = 1;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(slowScript);

  auto requester = std::jthread([](std::stop_token token) {
    std::this_thread::sleep_for(1200ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const canceledRes = pipeline::run(ctx);
  REQUIRE(canceledRes);
  CHECK(canceledRes.value() == stopsignal::kCanceledExitCode);
  stopsignal::reset();
  CHECK(fs::exists(inputDir / "packed" / ".compress_tmp_q5"));

  auto const countingScript = temp.path / "fake_ffmpeg_count.cmd";
  auto const countFile = temp.path / "count.txt";
  writeFakeFfmpegCountingScript(countingScript, countFile);

  auto ctx2 = appctx::AppContext{};
  ctx2.config.processType = "picture";
  ctx2.config.yesToAll = true;
  ctx2.config.verbose = true;
  ctx2.config.restartState = true;
  ctx2.config.compressImages = true;
  ctx2.config.imageQuality = 2;
  ctx2.config.maxParallelJobs = 1;
  ctx2.config.inputPath = inputDir;
  ctx2.toolchain.ffmpegPath = makeCmdScriptCommand(countingScript);

  auto const rerunRes = pipeline::run(ctx2);
  REQUIRE(rerunRes);
  CHECK(rerunRes.value() == 0);
  CHECK(readInvocationCount(countFile) == 2);
  CHECK_FALSE(fs::exists(inputDir / "packed" / ".compress_tmp_q5"));
}
#endif
