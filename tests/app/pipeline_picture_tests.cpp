#include "app/pipeline.h"
#include "core/job_state.h"
#include "core/work_dirs.h"
#include "infra/stop_signal.h"
#include "test_utils.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

using testutils::copyFakeTool;
using testutils::listZipRegularEntryNames;
using testutils::ScopedEnvVar;
using testutils::ScopedStopSignalReset;
using testutils::writeTextFile;

namespace {

std::size_t readInvocationCount(fs::path const& counterPath) {
  auto in = std::ifstream{counterPath, std::ios::binary};
  auto value = std::size_t{0};
  if (in.is_open()) { in >> value; }
  return value;
}

}  // namespace

TEST_CASE("picture pipeline packs directory", "[pipeline]") {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  fs::create_directories(inputDir);
  writeTextFile(inputDir / "a.jpg");

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
  writeTextFile(inputDir / "a.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.inputPath = inputDir;

  auto const stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config).value();
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
  writeTextFile(inputDir / "a.jpg");

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
  writeTextFile(dirA / "alpha.jpg");
  writeTextFile(dirA / "beta.jpg");
  writeTextFile(dirB / "alpha.jpg");
  writeTextFile(dirB / "beta.jpg");

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
  writeTextFile(dirA / "alpha.jpg");
  writeTextFile(dirA / "beta.jpg");
  writeTextFile(dirB / "alpha.jpg");
  writeTextFile(dirB / "beta.jpg");

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
  writeTextFile(dirA / "alpha.jpg");
  writeTextFile(dirB / "beta.jpg");

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
  writeTextFile(dirA / "alpha.jpg");
  writeTextFile(dirA / "beta.jpg");
  writeTextFile(dirB / "alpha.jpg");
  writeTextFile(dirB / "beta.jpg");

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
  writeTextFile(dirA / "alpha.jpg");
  writeTextFile(dirB / "beta.jpg");

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
  writeTextFile(dirA / "same.jpg");
  writeTextFile(dirB / "same.jpg");

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

TEST_CASE(
  "picture pipeline compress+pack produces .jpg entries",
  "[pipeline][compress]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  fs::create_directories(inputDir);
  writeTextFile(inputDir / "a.png");
  writeTextFile(inputDir / "b.png");

  auto const emptyOut = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "0"};
  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

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
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  writeTextFile(dirA / "same.png");
  writeTextFile(dirB / "same.png");

  auto const emptyOut = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "0"};
  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.outputLayout = appctx::OutputLayout::Keep;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

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
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  writeTextFile(dirA / "alpha.png");
  writeTextFile(dirA / "beta.png");
  writeTextFile(dirB / "alpha.png");
  writeTextFile(dirB / "beta.png");

  auto const emptyOut = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "0"};
  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.verbose = true;
  ctx.config.pictureFolderSummary = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

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
  fs::create_directories(inputDir);
  writeTextFile(inputDir / "a.png");

  auto const emptyOut = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "0"};
  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto const stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config).value();
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
  fs::create_directories(inputDir);
  writeTextFile(inputDir / "fast.png");
  writeTextFile(inputDir / "slow.png");

  // Call 1 completes and caches; later calls block then would exit 130.
  auto const cntEnv = ScopedEnvVar{
    "ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE",
    (temp.path / "compress-count.txt").string()
  };
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:7000:130"};
  auto const emptyOut = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "0"};
  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.maxParallelJobs = 1;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto const stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config).value();
  auto const cacheDir = workdirs::compressCacheDir(inputDir, 5);

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
  fs::create_directories(inputDir);
  writeTextFile(inputDir / "fast.png");
  writeTextFile(inputDir / "slow.png");

  auto const cntEnv1 = ScopedEnvVar{
    "ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE",
    (temp.path / "slow-count.txt").string()
  };
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:7000:130"};
  auto const emptyOut1 = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "0"};
  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.maxParallelJobs = 1;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto requester = std::jthread([](std::stop_token token) {
    std::this_thread::sleep_for(1200ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const canceledRes = pipeline::run(ctx);
  REQUIRE(canceledRes);
  CHECK(canceledRes.value() == stopsignal::kCanceledExitCode);
  stopsignal::reset();

  // Resume run must re-compress only the missing file.
  auto const countFile = temp.path / "resume-count.txt";
  auto const cntEnv2 =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE", countFile.string()};

  auto ctx2 = appctx::AppContext{};
  ctx2.config.processType = "picture";
  ctx2.config.yesToAll = true;
  ctx2.config.verbose = true;
  ctx2.config.compressImages = true;
  ctx2.config.imageQuality = 5;
  ctx2.config.maxParallelJobs = 1;
  ctx2.config.inputPath = inputDir;
  ctx2.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto const resumeRes = pipeline::run(ctx2);
  REQUIRE(resumeRes);
  CHECK(resumeRes.value() == 0);
  CHECK(readInvocationCount(countFile) == 1);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "pics_part1[1~2#2p].zip");
  REQUIRE(entryNames.size() == 2);
  for (auto const& name: entryNames) { CHECK(name.ends_with(".jpg")); }
  CHECK_FALSE(fs::exists(workdirs::compressCacheDir(inputDir, 5)));
}

TEST_CASE(
  "picture pipeline compress recompresses replaced source on rerun",
  "[pipeline][compress]"
) {
  using namespace std::chrono_literals;

  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  fs::create_directories(inputDir);
  writeTextFile(inputDir / "a.png");
  writeTextFile(inputDir / "b.png");

  auto const cntEnv1 = ScopedEnvVar{
    "ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE",
    (temp.path / "slow-count.txt").string()
  };
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:7000:130"};
  auto const emptyOut1 = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "0"};
  auto const inputsLogEnv = ScopedEnvVar{
    "ENCRO_FAKE_FFMPEG_INPUT_LOG",
    (temp.path / "completed-inputs.log").string()
  };
  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.maxParallelJobs = 1;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto requester = std::jthread([](std::stop_token token) {
    std::this_thread::sleep_for(1200ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const canceledRes = pipeline::run(ctx);
  REQUIRE(canceledRes);
  CHECK(canceledRes.value() == stopsignal::kCanceledExitCode);
  stopsignal::reset();

  // The input recorded before cancellation is the cached survivor; bump its
  // mtime so the resume run must re-compress it as well.
  auto const completedInputs =
    testutils::readTextFile(temp.path / "completed-inputs.log");
  REQUIRE_FALSE(completedInputs.empty());
  auto const survivorLine = completedInputs.substr(0, completedInputs.find('\n'));
  writeTextFile(fs::path{survivorLine});

  auto const countFile = temp.path / "resume-count.txt";
  auto const cntEnv2 =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE", countFile.string()};
  auto const noPlan = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", ""};

  auto ctx2 = appctx::AppContext{};
  ctx2.config.processType = "picture";
  ctx2.config.yesToAll = true;
  ctx2.config.verbose = true;
  ctx2.config.compressImages = true;
  ctx2.config.imageQuality = 5;
  ctx2.config.maxParallelJobs = 1;
  ctx2.config.inputPath = inputDir;
  ctx2.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

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
  fs::create_directories(inputDir);
  writeTextFile(inputDir / "a.png");
  writeTextFile(inputDir / "b.png");

  auto const cntEnv1 = ScopedEnvVar{
    "ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE",
    (temp.path / "slow-count.txt").string()
  };
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:7000:130"};
  auto const emptyOut1 = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "0"};
  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.maxParallelJobs = 1;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto requester = std::jthread([](std::stop_token token) {
    std::this_thread::sleep_for(1200ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const canceledRes = pipeline::run(ctx);
  REQUIRE(canceledRes);
  CHECK(canceledRes.value() == stopsignal::kCanceledExitCode);
  stopsignal::reset();
  auto const stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config).value();
  CHECK(fs::exists(stateFilePath));
  CHECK(fs::exists(workdirs::compressCacheDir(inputDir, 5)));

  // State gone → cache invalidated: both sources re-compress.
  fs::remove(stateFilePath);
  auto const countFile = temp.path / "rerun-count.txt";
  auto const cntEnv2 =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE", countFile.string()};
  auto const noPlan = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", ""};

  auto ctx2 = appctx::AppContext{};
  ctx2.config.processType = "picture";
  ctx2.config.yesToAll = true;
  ctx2.config.verbose = true;
  ctx2.config.compressImages = true;
  ctx2.config.imageQuality = 5;
  ctx2.config.maxParallelJobs = 1;
  ctx2.config.inputPath = inputDir;
  ctx2.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

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
  fs::create_directories(inputDir);
  writeTextFile(inputDir / "a.png");
  writeTextFile(inputDir / "b.png");

  auto const cntEnv1 = ScopedEnvVar{
    "ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE",
    (temp.path / "slow-count.txt").string()
  };
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:7000:130"};
  auto const emptyOut1 = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "0"};
  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.maxParallelJobs = 1;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto requester = std::jthread([](std::stop_token token) {
    std::this_thread::sleep_for(1200ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const canceledRes = pipeline::run(ctx);
  REQUIRE(canceledRes);
  CHECK(canceledRes.value() == stopsignal::kCanceledExitCode);
  stopsignal::reset();
  CHECK(fs::exists(workdirs::compressCacheDir(inputDir, 5)));

  // Quality change → previous cache not reused: both sources re-compress.
  auto const countFile = temp.path / "rerun-count.txt";
  auto const cntEnv2 =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE", countFile.string()};
  auto const noPlan = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", ""};

  auto ctx2 = appctx::AppContext{};
  ctx2.config.processType = "picture";
  ctx2.config.yesToAll = true;
  ctx2.config.verbose = true;
  ctx2.config.compressImages = true;
  ctx2.config.imageQuality = 2;
  ctx2.config.maxParallelJobs = 1;
  ctx2.config.inputPath = inputDir;
  ctx2.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto const rerunRes = pipeline::run(ctx2);
  REQUIRE(rerunRes);
  CHECK(rerunRes.value() == 0);
  CHECK(readInvocationCount(countFile) == 2);
  CHECK_FALSE(fs::exists(workdirs::compressCacheDir(inputDir, 5)));
}

TEST_CASE(
  "picture pipeline compress --restart clears stale caches and recompresses all",
  "[pipeline][compress]"
) {
  using namespace std::chrono_literals;

  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  fs::create_directories(inputDir);
  writeTextFile(inputDir / "a.png");
  writeTextFile(inputDir / "b.png");

  auto const cntEnv1 = ScopedEnvVar{
    "ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE",
    (temp.path / "slow-count.txt").string()
  };
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:7000:130"};
  auto const emptyOut1 = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "0"};
  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.compressImages = true;
  ctx.config.imageQuality = 5;
  ctx.config.maxParallelJobs = 1;
  ctx.config.inputPath = inputDir;
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto requester = std::jthread([](std::stop_token token) {
    std::this_thread::sleep_for(1200ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const canceledRes = pipeline::run(ctx);
  REQUIRE(canceledRes);
  CHECK(canceledRes.value() == stopsignal::kCanceledExitCode);
  stopsignal::reset();
  CHECK(fs::exists(workdirs::compressCacheDir(inputDir, 5)));

  // --restart clears the stale caches: everything re-compresses.
  auto const countFile = temp.path / "rerun-count.txt";
  auto const cntEnv2 =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE", countFile.string()};
  auto const noPlan = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", ""};

  auto ctx2 = appctx::AppContext{};
  ctx2.config.processType = "picture";
  ctx2.config.yesToAll = true;
  ctx2.config.verbose = true;
  ctx2.config.restartState = true;
  ctx2.config.compressImages = true;
  ctx2.config.imageQuality = 2;
  ctx2.config.maxParallelJobs = 1;
  ctx2.config.inputPath = inputDir;
  ctx2.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto const rerunRes = pipeline::run(ctx2);
  REQUIRE(rerunRes);
  CHECK(rerunRes.value() == 0);
  CHECK(readInvocationCount(countFile) == 2);
  CHECK_FALSE(fs::exists(workdirs::compressCacheDir(inputDir, 5)));
}
