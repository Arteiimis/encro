#include "app/pipeline.h"
#include "core/job_state.h"
#include "core/work_dirs.h"
#include "infra/stop_signal.h"
#include "test_utils.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

using testutils::collisionGroupPrefix;
using testutils::copyFakeTool;
using testutils::hasCollisionSafePrefix;
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

// Counts ffmpeg-role lines in the fake tool's invocation log; a missing log
// counts as zero (poll predicates must not abort on absent files).
std::size_t countFfmpegInvocations(fs::path const& logPath) {
  auto in = std::ifstream{logPath, std::ios::binary};
  if (!in.is_open()) { return 0; }
  auto const content = std::string{std::istreambuf_iterator<char>{in}, {}};
  auto count = std::size_t{0};
  auto pos = std::string::size_type{0};
  while ((pos = content.find("ffmpeg\t", pos)) != std::string::npos) {
    ++count;
    pos += 1;
  }
  return count;
}

// Stop requester for the gated mid-batch cancel tests: waits for proof that
// the second compress call is in flight (two logged ffmpeg invocations),
// then raises the stop and releases the gate. Replaces the old fixed
// 1200 ms sleep-and-hope, which could fire before the first call finished
// under parallel shard load. *secondCallProven stays false when the proof
// never arrives within 10 s; callers REQUIRE it after join.
auto spawnGatedStop(
  fs::path const& logPath,
  fs::path const& gatePath,
  bool& secondCallProven
) -> std::jthread {
  return std::jthread{[logPath, gatePath, &secondCallProven] {
    secondCallProven = testutils::waitUntil(
      [&] { return countFfmpegInvocations(logPath) >= 2; },
      std::chrono::seconds{10}
    );
    stopsignal::requestStop();
    auto gate = std::ofstream{gatePath, std::ios::binary};
    gate << "go";
  }};
}

}  // namespace

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

TEST_CASE(
  "picture pipeline groups per source dir in flat mode and preserves keep-layout paths",
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
  ctx.config.pictureFolderSummary = false;
  ctx.config.inputPath = inputDir;

  SECTION("flat mode keeps same-dir files in one zip under one collision group") {
    auto runRes = pipeline::run(ctx);
    REQUIRE(runRes);
    REQUIRE(runRes.value() == 0);

    auto const packedDir = inputDir / "packed";
    REQUIRE(fs::exists(packedDir));
    auto zipCount = 0;
    auto allEntries = std::vector<std::string>{};
    for (auto const& de: fs::directory_iterator{packedDir}) {
      if (de.path().extension() != ".zip") { continue; }
      ++zipCount;
      auto zipEntries = listZipRegularEntryNames(de.path());
      allEntries.insert(allEntries.end(), zipEntries.begin(), zipEntries.end());
    }
    std::ranges::sort(allEntries);

    // Small fixtures pack into a single archive: same-dir files share a zip.
    REQUIRE(zipCount == 1);
    REQUIRE(allEntries.size() == 4);
    // Entries carry the collision-safe flat prefix; per-directory grouping is
    // visible as one collision group per source dir.
    CHECK(hasCollisionSafePrefix(allEntries[0], "a", "alpha"));
    CHECK(hasCollisionSafePrefix(allEntries[1], "a", "beta"));
    CHECK(hasCollisionSafePrefix(allEntries[2], "b", "alpha"));
    CHECK(hasCollisionSafePrefix(allEntries[3], "b", "beta"));
    CHECK(collisionGroupPrefix(allEntries[0]) == collisionGroupPrefix(allEntries[1]));
    CHECK(collisionGroupPrefix(allEntries[2]) == collisionGroupPrefix(allEntries[3]));
    CHECK(collisionGroupPrefix(allEntries[0]) != collisionGroupPrefix(allEntries[2]));
  }

  SECTION("compress with keep layout preserves relative paths as .jpg entries") {
    auto const emptyOut = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "0"};
    ctx.config.verbose = true;
    ctx.config.compressImages = true;
    ctx.config.imageQuality = 5;
    ctx.config.outputLayout = appctx::OutputLayout::Keep;
    ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

    auto runRes = pipeline::run(ctx);
    REQUIRE(runRes);
    CHECK(runRes.value() == 0);

    // Keep layout: entry names are the source-relative paths (.jpg after
    // compress), the same structure property the Keep naming strategy pins.
    auto const entryNames =
      listZipRegularEntryNames(inputDir / "packed" / "pics_part1[1~4#4p].zip");
    REQUIRE(entryNames.size() == 4);
    CHECK(entryNames[0] == "a/alpha.jpg");
    CHECK(entryNames[1] == "a/beta.jpg");
    CHECK(entryNames[2] == "b/alpha.jpg");
    CHECK(entryNames[3] == "b/beta.jpg");
  }
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
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:3000:130"};
  // The stop lands inside a gate-held second call: no timing window. The
  // gate file created below persists into any resume phase, where fresh
  // call sequences pass through it instantly.
  auto const toolLogEnv =
    ScopedEnvVar{"ENCRO_FAKE_TOOL_LOG_FILE", (temp.path / "tool.log").string()};
  auto const gateEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FILE", (temp.path / "gate").string()};
  auto const gateFromCallEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FROM_CALL", "2"};
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

  auto secondCallProven = false;
  auto requester =
    spawnGatedStop(temp.path / "tool.log", temp.path / "gate", secondCallProven);

  auto const runRes = pipeline::run(ctx);
  requester.join();
  REQUIRE(secondCallProven);

  REQUIRE(runRes);
  CHECK(runRes.value() == stopsignal::kCanceledExitCode);
  CHECK(fs::exists(stateFilePath));
  CHECK(fs::exists(cacheDir));
  auto cachedCount = 0;
  for (auto const& de: fs::directory_iterator(cacheDir)) { ++cachedCount; }
  CHECK(cachedCount >= 1);
  // Cancellation cuts through the in-flight compression: nothing is packed.
  CHECK_FALSE(fs::exists(inputDir / "packed" / "pics_part1[1~1#1p].zip"));
}

TEST_CASE(
  "picture pipeline compress rerun resumes from cache and packs completed files",
  "[pipeline][compress]"
) {
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
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:3000:130"};
  // The stop lands inside a gate-held second call: no timing window. The
  // gate file created below persists into any resume phase, where fresh
  // call sequences pass through it instantly.
  auto const toolLogEnv =
    ScopedEnvVar{"ENCRO_FAKE_TOOL_LOG_FILE", (temp.path / "tool.log").string()};
  auto const gateEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FILE", (temp.path / "gate").string()};
  auto const gateFromCallEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FROM_CALL", "2"};
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

  auto secondCallProven = false;
  auto requester =
    spawnGatedStop(temp.path / "tool.log", temp.path / "gate", secondCallProven);

  auto const canceledRes = pipeline::run(ctx);
  requester.join();
  REQUIRE(secondCallProven);
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
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:3000:130"};
  // The stop lands inside a gate-held second call: no timing window. The
  // gate file created below persists into any resume phase, where fresh
  // call sequences pass through it instantly.
  auto const toolLogEnv =
    ScopedEnvVar{"ENCRO_FAKE_TOOL_LOG_FILE", (temp.path / "tool.log").string()};
  auto const gateEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FILE", (temp.path / "gate").string()};
  auto const gateFromCallEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FROM_CALL", "2"};
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

  auto secondCallProven = false;
  auto requester =
    spawnGatedStop(temp.path / "tool.log", temp.path / "gate", secondCallProven);

  auto const canceledRes = pipeline::run(ctx);
  requester.join();
  REQUIRE(secondCallProven);
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
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:3000:130"};
  // The stop lands inside a gate-held second call: no timing window. The
  // gate file created below persists into any resume phase, where fresh
  // call sequences pass through it instantly.
  auto const toolLogEnv =
    ScopedEnvVar{"ENCRO_FAKE_TOOL_LOG_FILE", (temp.path / "tool.log").string()};
  auto const gateEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FILE", (temp.path / "gate").string()};
  auto const gateFromCallEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FROM_CALL", "2"};
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

  auto secondCallProven = false;
  auto requester =
    spawnGatedStop(temp.path / "tool.log", temp.path / "gate", secondCallProven);

  auto const canceledRes = pipeline::run(ctx);
  requester.join();
  REQUIRE(secondCallProven);
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
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:3000:130"};
  // The stop lands inside a gate-held second call: no timing window. The
  // gate file created below persists into any resume phase, where fresh
  // call sequences pass through it instantly.
  auto const toolLogEnv =
    ScopedEnvVar{"ENCRO_FAKE_TOOL_LOG_FILE", (temp.path / "tool.log").string()};
  auto const gateEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FILE", (temp.path / "gate").string()};
  auto const gateFromCallEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FROM_CALL", "2"};
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

  auto secondCallProven = false;
  auto requester =
    spawnGatedStop(temp.path / "tool.log", temp.path / "gate", secondCallProven);

  auto const canceledRes = pipeline::run(ctx);
  requester.join();
  REQUIRE(secondCallProven);
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
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:3000:130"};
  // The stop lands inside a gate-held second call: no timing window. The
  // gate file created below persists into any resume phase, where fresh
  // call sequences pass through it instantly.
  auto const toolLogEnv =
    ScopedEnvVar{"ENCRO_FAKE_TOOL_LOG_FILE", (temp.path / "tool.log").string()};
  auto const gateEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FILE", (temp.path / "gate").string()};
  auto const gateFromCallEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FROM_CALL", "2"};
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

  auto secondCallProven = false;
  auto requester =
    spawnGatedStop(temp.path / "tool.log", temp.path / "gate", secondCallProven);

  auto const canceledRes = pipeline::run(ctx);
  requester.join();
  REQUIRE(secondCallProven);
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
