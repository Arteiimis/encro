#include "picture/picture_process.h"
#include "infra/stop_signal.h"
#include "pack/pack.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
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

void writeFakeFfmpegSecondCallSlowScript(
  fs::path const& scriptPath,
  fs::path const& counterPath
) {
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
if !count! GEQ 2 ping -n 8 127.0.0.1 >nul
for %%I in ("%outputPath%") do if not "%%~dpI"=="" mkdir "%%~dpI" 2>nul
>%outputPath% echo fake-compressed-jpeg
exit /b 0
)",
    counterPath.string()
  );
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

  auto const result = pack::execute({
    .entries = {f1, f2, f3},
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .compact = true,
    .removeOnFailure = true,
  });

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

  auto const result = pack::execute({
    .entries = {a1, a2, b1, b2},
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .compact = true,
    .removeOnFailure = true,
    .naming = pack::NamingConfig{
      .namingStrategy = pack::NamingStrategy::Flat,
      .baseName = "pics",
    },
  });

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

TEST_CASE(
  "runPicturePackWorkflow with folder-summary keeps summary entries ahead of regular "
  "files",
  "[picture-process]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  createSparseSizedFile(dirA, "alpha.jpg", 32);
  createSparseSizedFile(dirA, "beta.jpg", 32);
  createSparseSizedFile(dirB, "alpha.jpg", 32);
  createSparseSizedFile(dirB, "beta.jpg", 32);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.pictureFolderSummary = true;
  ctx.config.inputPath = inputDir;

  auto const runRes = runPicturePackWorkflow(ctx, inputDir);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto entryNames = std::vector<std::string>{};
  auto const packedDir = inputDir / "packed";
  REQUIRE(fs::exists(packedDir));
  for (auto const& de: fs::directory_iterator{packedDir}) {
    if (de.path().extension() != ".zip") { continue; }

    auto zipEntries = testutils::listZipRegularEntryNames(de.path());
    entryNames.insert(entryNames.end(), zipEntries.begin(), zipEntries.end());
  }

  std::ranges::sort(entryNames);
  REQUIRE(entryNames.size() == 6);
  CHECK(entryNames[0].starts_with("0000__summary__"));
  CHECK(entryNames[1].starts_with("0000__summary__"));
  CHECK(entryNames[2].starts_with("1000__"));
  CHECK(entryNames[3].starts_with("1000__"));
  CHECK(entryNames[4].starts_with("1000__"));
  CHECK(entryNames[5].starts_with("1000__"));
}

TEST_CASE(
  "runPicturePackWorkflow with folder-summary counts summaries toward logical pack "
  "limit",
  "[picture-process]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);

  for (auto index = 0; index < 1999; ++index) {
    createSparseSizedFile(dirA, std::format("a_{:04d}.jpg", index), 1);
  }
  createSparseSizedFile(dirB, "b_0000.jpg", 1);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.pictureFolderSummary = true;
  ctx.config.inputPath = inputDir;

  auto const runRes = runPicturePackWorkflow(ctx, inputDir);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto const part1 = inputDir / "packed" / "pics_part1[1~2000#2000p].zip";
  auto const part2 = inputDir / "packed" / "pics_part2[2001~2002#2p].zip";
  REQUIRE(fs::exists(part1));
  REQUIRE(fs::exists(part2));

  auto const part1Entries = testutils::listZipRegularEntryNames(part1);
  auto const part2Entries = testutils::listZipRegularEntryNames(part2);

  REQUIRE(part1Entries.size() == 2000);
  REQUIRE(part2Entries.size() == 2);
  CHECK(part1Entries.front().starts_with("0000__summary__a__"));
  CHECK(part2Entries.front().starts_with("0000__summary__b__"));
  CHECK(std::ranges::none_of(part1Entries, [](std::string const& name) {
    return name.starts_with("0000__summary__b__");
  }));
  CHECK(std::ranges::none_of(part2Entries, [](std::string const& name) {
    return name.starts_with("0000__summary__a__");
  }));
}

TEST_CASE(
  "runPicturePackWorkflow with folder-summary keeps summaries in first physical "
  "subpart of a logical pack",
  "[picture-process]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);

  createSparseSizedFile(dirA, "0001.jpg", 1);
  createSparseSizedFile(dirA, "9999.jpg", 260ULL * 1024ULL * 1024ULL);
  createSparseSizedFile(dirB, "0001.jpg", 1);
  createSparseSizedFile(dirB, "9999.jpg", 260ULL * 1024ULL * 1024ULL);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.pictureFolderSummary = true;
  ctx.config.inputPath = inputDir;

  auto const runRes = runPicturePackWorkflow(ctx, inputDir);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);

  auto part11 = fs::path{};
  auto part12 = fs::path{};
  for (auto const& de: fs::directory_iterator{inputDir / "packed"}) {
    auto const fileName = de.path().filename().string();
    if (fileName.starts_with("pics_part1.1[")) { part11 = de.path(); }
    if (fileName.starts_with("pics_part1.2[")) { part12 = de.path(); }
  }

  REQUIRE_FALSE(part11.empty());
  REQUIRE_FALSE(part12.empty());

  auto const part11Entries = testutils::listZipRegularEntryNames(part11);
  auto const part12Entries = testutils::listZipRegularEntryNames(part12);
  CHECK(std::ranges::any_of(part11Entries, [](std::string const& name) {
    return name.starts_with("0000__summary__");
  }));
  CHECK(std::ranges::any_of(part12Entries, [](std::string const& name) {
    return name.starts_with("0000__summary__");
  }));
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

  auto input = std::istringstream{"n\n"};
  auto* oldBuf = std::cin.rdbuf(input.rdbuf());
  auto const runRes = runPicturePackWorkflow(ctx, inputDir);
  std::cin.rdbuf(oldBuf);
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
  "runPicturePackWorkflow compress stops immediately when cancellation happens mid-batch",
  "[picture-process][compress]"
) {
  using namespace std::chrono_literals;

  testutils::ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const scriptPath = temp.path / "fake_ffmpeg_slow.cmd";
  auto const counterPath = temp.path / "compress-count.txt";
  fs::create_directories(inputDir);
  createSparseSizedFile(inputDir, "fast.png", 32);
  createSparseSizedFile(inputDir, "slow.png", 32);
  writeFakeFfmpegSecondCallSlowScript(scriptPath, counterPath);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.verboseEcho = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.maxParallelJobs = 1;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(scriptPath);

  auto requester = std::jthread([](std::stop_token token) {
    std::this_thread::sleep_for(1200ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const runRes = runPicturePackWorkflow(ctx, inputDir);

  REQUIRE(runRes);
  CHECK(runRes.value() == stopsignal::kCanceledExitCode);
  CHECK_FALSE(fs::exists(inputDir / "packed" / "part1[1~1#1p].zip"));
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
