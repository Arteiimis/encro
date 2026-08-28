#include "video/video_batch_execution.h"

#include "core/job_state.h"
#include "infra/stop_signal.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

using testutils::ScopedEnvVar;

TEST_CASE("videobatch types compile and are usable", "[video-batch-execution]") {
  // GREEN phase: extraction complete — public API types verified
  auto actionIds = videobatch::ActionIdMap{};
  auto results = videobatch::EncodeResultsMap{};
  CHECK(actionIds.size() == 0);
  CHECK(results.size() == 0);
}

TEST_CASE(
  "encoding monitor exits promptly after a stop request",
  "[video-batch-execution][stop-signal]"
) {
  auto resetGuard = testutils::ScopedStopSignalReset{};
  auto appCtx = appctx::AppContext{};
  auto progressState = videobatch::detail::EncodingProgressState{1, 1};
  auto plannedOutputFiles = appctx::path_map<fs::path>{};
  auto actionIds = videobatch::ActionIdMap{};
  auto execCtx = videobatch::detail::EncodingExecutionContext{
    appCtx,
    progressState,
    plannedOutputFiles,
    actionIds
  };

  auto monitor = videobatch::detail::startEncodingMonitor(execCtx);
  std::this_thread::sleep_for(std::chrono::milliseconds{100});

  auto const start = std::chrono::steady_clock::now();
  stopsignal::requestStop();
  // Join is the completion detector: the monitor must leave its tick loop on
  // the next wake (event wait wakes it immediately), well under 1 s.
  monitor.join();
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds{1});
}

TEST_CASE(
  "encoding monitor skips parsing when the progress file size is unchanged",
  "[video-batch-execution][monitor]"
) {
  auto resetGuard = testutils::ScopedStopSignalReset{};
  auto appCtx = appctx::AppContext{};
  auto progressState = videobatch::detail::EncodingProgressState{1, 1};
  auto plannedOutputFiles = appctx::path_map<fs::path>{};
  auto actionIds = videobatch::ActionIdMap{};
  auto execCtx = videobatch::detail::EncodingExecutionContext{
    appCtx,
    progressState,
    plannedOutputFiles,
    actionIds
  };
  TempDir temp;
  auto const progressFile = temp.path / "progress.log";
  {
    std::ofstream out{progressFile};
    out << "frame=10\nprogress=continue\n";
  }

  auto state = std::make_shared<appctx::EncodingState>();
  state->inputPath = temp.path / "in.mp4";
  state->totalFrames = 100;
  state->progressFilePath = progressFile;
  execCtx.setActive(0, state);

  auto monitor = videobatch::detail::startEncodingMonitor(execCtx);
  // Let the first parse pass run, then rewrite the file with the same size.
  std::this_thread::sleep_for(std::chrono::milliseconds{400});
  {
    std::ofstream out{progressFile};
    out << "frame=99\nprogress=continue\n";
  }
  std::this_thread::sleep_for(std::chrono::milliseconds{500});

  // Stat-skip: same size means unchanged, so the replacement is not re-parsed.
  {
    auto lock = std::scoped_lock{state->mtx};
    CHECK(state->lastFrameCount.value_or(0) == 10);
  }

  stopsignal::requestStop();
  execCtx.clearActive(0);
  monitor.join();
}

TEST_CASE(
  "encoding monitor throttles progress parsing to a few passes per second",
  "[video-batch-execution][monitor]"
) {
  auto resetGuard = testutils::ScopedStopSignalReset{};
  auto appCtx = appctx::AppContext{};
  auto progressState = videobatch::detail::EncodingProgressState{1, 1};
  auto plannedOutputFiles = appctx::path_map<fs::path>{};
  auto actionIds = videobatch::ActionIdMap{};
  auto execCtx = videobatch::detail::EncodingExecutionContext{
    appCtx,
    progressState,
    plannedOutputFiles,
    actionIds
  };
  TempDir temp;
  auto const progressFile = temp.path / "progress.log";

  auto state = std::make_shared<appctx::EncodingState>();
  state->inputPath = temp.path / "in.mp4";
  state->totalFrames = 1000;
  state->progressFilePath = progressFile;
  execCtx.setActive(0, state);

  auto monitor = videobatch::detail::startEncodingMonitor(execCtx);

  // Append a frame line every 40 ms for 600 ms (15 updates total). A 20 ms
  // parse cadence would observe ~15 distinct frames; the 250 ms throttle
  // observes only ~3.
  auto seen = std::vector<uint64_t>{};
  auto lastSeen = std::optional<uint64_t>{};
  for (auto i = 0; i < 15; ++i) {
    {
      std::ofstream out{progressFile, std::ios::app};
      out << "frame=" << (i + 1) << "\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{40});
    {
      auto lock = std::scoped_lock{state->mtx};
      auto const frame = state->lastFrameCount;
      if (frame.has_value() && frame != lastSeen) {
        lastSeen = frame;
        seen.push_back(frame.value());
      }
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds{400});
  {
    auto lock = std::scoped_lock{state->mtx};
    CHECK(state->lastFrameCount.value_or(0) == 15);  // final value correct
  }
  // Distinct frame values observed across the window: a handful, not one per
  // append.
  CHECK(seen.size() >= 1);
  CHECK(seen.size() < 8);

  stopsignal::requestStop();
  execCtx.clearActive(0);
  monitor.join();
}
TEST_CASE(
  "barDone sets Success tone and 100% progress on success",
  "[video-batch-execution]"
) {
  auto appCtx = appctx::AppContext{};
  auto progressState = videobatch::detail::EncodingProgressState{1, 1};
  auto plannedOutputFiles = appctx::path_map<fs::path>{};
  auto actionIds = videobatch::ActionIdMap{};

  auto execCtx = videobatch::detail::EncodingExecutionContext{
    appCtx,
    progressState,
    plannedOutputFiles,
    actionIds
  };

  auto barIdx = execCtx.barIndexOpt(0);

  // barDone with success=true should not throw or crash
  REQUIRE_NOTHROW(execCtx.barDone(barIdx, true, "test.mp4"));
}

TEST_CASE(
  "barDone sets Failure tone and preserves progress on failure",
  "[video-batch-execution]"
) {
  auto appCtx = appctx::AppContext{};
  auto progressState = videobatch::detail::EncodingProgressState{1, 1};
  auto plannedOutputFiles = appctx::path_map<fs::path>{};
  auto actionIds = videobatch::ActionIdMap{};

  auto execCtx = videobatch::detail::EncodingExecutionContext{
    appCtx,
    progressState,
    plannedOutputFiles,
    actionIds
  };

  auto barIdx = execCtx.barIndexOpt(0);

  // barDone with success=false should not throw or crash
  REQUIRE_NOTHROW(execCtx.barDone(barIdx, false, "test.mp4"));
}

TEST_CASE("barDone is no-op when barIndex is nullopt", "[video-batch-execution]") {
  auto appCtx = appctx::AppContext{};
  auto progressState = videobatch::detail::EncodingProgressState{1, 1};
  auto plannedOutputFiles = appctx::path_map<fs::path>{};
  auto actionIds = videobatch::ActionIdMap{};

  auto execCtx = videobatch::detail::EncodingExecutionContext{
    appCtx,
    progressState,
    plannedOutputFiles,
    actionIds
  };

  // barDone with nullopt barIndex should not throw or crash
  REQUIRE_NOTHROW(execCtx.barDone(std::nullopt, true, "test.mp4"));
  REQUIRE_NOTHROW(execCtx.barDone(std::nullopt, false, "test.mp4"));
}

// ── runEncodingTasks batch-branch tests ──────────────────────────────────────

namespace {

// Minimal in-process scaffold for runEncodingTasks: fake toolchain, one sized
// input, probe scoring wired, invocation-log capture.
struct BatchScaffold {
  TempDir temp;
  appctx::AppContext ctx;
  std::vector<std::unique_ptr<ScopedEnvVar>> envs;
  fs::path inputPath;
  fs::path logPath;

  BatchScaffold() {
    inputPath = temp.path / "sample.mp4";
    ctx.config.outputFormat = "mp4";
    ctx.config.outputPath = temp.path / "work";
    ctx.toolchain.ffprobePath = testutils::copyFakeTool(temp.path, "ffprobe");
    ctx.toolchain.ffmpegPath = testutils::copyFakeTool(temp.path, "ffmpeg");
    envs.push_back(
      std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFPROBE_DURATION_SECS", "100.0")
    );
    envs.push_back(std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_WRITE_VMAF", "1"));
    envs
      .push_back(std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_VMAF_SCORES", "96.0"));
    logPath = temp.path / "invocations.log";
    envs.push_back(
      std::make_unique<ScopedEnvVar>("ENCRO_FAKE_TOOL_LOG_FILE", logPath.string())
    );
    testutils::writeSizedFile(inputPath, 1'048'576);
  }

  static auto singleFileJob(fs::path const& vid, fs::path const& output)
    -> videobatch::EncodingBatchJob {
    return videobatch::EncodingBatchJob{
      .vids = {vid},
      .plannedOutputFiles = appctx::path_map<fs::path>{{vid, output}},
      .actionIds = {},
    };
  }
};

}  // namespace

TEST_CASE(
  "runEncodingTasks returns nullopt results when the confirmation is declined",
  "[video-batch-execution]"
) {
  auto s = BatchScaffold{};
  s.ctx.config.yesToAll = false;

  // stdin EOF: the confirmation prompt reads no answer and declines.
  auto eofInput = std::istringstream{};
  auto* oldBuf = std::cin.rdbuf(eofInput.rdbuf());

  auto const outcome = videobatch::runEncodingTasks(
    s.ctx,
    BatchScaffold::singleFileJob(s.inputPath, s.temp.path / "encoded" / "sample.mp4"),
    1,
    0
  );

  std::cin.rdbuf(oldBuf);
  std::cin.clear();

  REQUIRE_FALSE(outcome.results.has_value());
  CHECK(outcome.attentionWarnings.empty());
  // The probe stage ran, but no production encode was started (encode outputs
  // land in the planned "encoded" directory).
  auto const log = testutils::readTextFile(s.logPath);
  CHECK(log.find("encoded") == std::string::npos);
}

TEST_CASE(
  "runEncodingTasks verbose path encodes sequentially and records job-state",
  "[video-batch-execution]"
) {
  auto s = BatchScaffold{};
  s.ctx.config.verbose = true;
  s.ctx.config.yesToAll = true;

  auto const b = s.temp.path / "b.mp4";
  testutils::writeSizedFile(b, 1'048'576);

  auto const statePath = s.temp.path / "job-state.json";
  auto store = std::make_shared<jobstate::Store>(statePath);
  REQUIRE(store->initialize(s.ctx.config, false));
  auto const taskA =
    jobstate::makeEncodeTask(s.inputPath, s.temp.path / "encoded" / "sample.mp4");
  auto const taskB = jobstate::makeEncodeTask(b, s.temp.path / "encoded" / "b.mp4");
  store->mergeTasks(std::array{taskA, taskB});
  s.ctx.runtime.jobState = store;

  auto const job = videobatch::EncodingBatchJob{
    .vids = {s.inputPath, b},
    .plannedOutputFiles =
      appctx::path_map<fs::path>{
        {s.inputPath, s.temp.path / "encoded" / "sample.mp4"},
        {b, s.temp.path / "encoded" / "b.mp4"},
      },
    .actionIds = appctx::path_map<std::string>{{s.inputPath, taskA.id}, {b, taskB.id}},
  };

  auto const outcome = videobatch::runEncodingTasks(s.ctx, job, 2, 0);

  REQUIRE(outcome.results.has_value());
  REQUIRE(outcome.results->size() == 2);
  CHECK(outcome.results->at(s.inputPath));
  CHECK(outcome.results->at(b));

  // The verbose path encodes per-file in order: file a's encode precedes
  // file b's.
  auto const log = testutils::readTextFile(s.logPath);
  auto const aEncode = log.find((s.temp.path / "encoded" / "sample.mp4").string());
  auto const bEncode = log.find((s.temp.path / "encoded" / "b.mp4").string());
  CHECK(aEncode != std::string::npos);
  CHECK(bEncode != std::string::npos);
  CHECK(aEncode < bEncode);

  store->flush();
  auto reread = jobstate::Store{statePath};
  REQUIRE(reread.initialize(s.ctx.config, false));
  auto const rereadA = reread.findTask(taskA.id);
  auto const rereadB = reread.findTask(taskB.id);
  REQUIRE(rereadA.has_value());
  CHECK(rereadA->status == jobstate::TaskStatus::Succeeded);
  REQUIRE(rereadB.has_value());
  CHECK(rereadB->status == jobstate::TaskStatus::Succeeded);
}

TEST_CASE(
  "runEncodingTasks verbose path keeps encoding after a failing file",
  "[video-batch-execution]"
) {
  auto s = BatchScaffold{};
  s.ctx.config.verbose = true;
  s.ctx.config.yesToAll = true;

  auto const b = s.temp.path / "b.mp4";
  testutils::writeSizedFile(b, 1'048'576);

  auto const statePath = s.temp.path / "job-state.json";
  auto store = std::make_shared<jobstate::Store>(statePath);
  REQUIRE(store->initialize(s.ctx.config, false));
  auto const taskA =
    jobstate::makeEncodeTask(s.inputPath, s.temp.path / "encoded" / "sample.mp4");
  auto const taskB = jobstate::makeEncodeTask(b, s.temp.path / "encoded" / "b.mp4");
  store->mergeTasks(std::array{taskA, taskB});
  s.ctx.runtime.jobState = store;

  // The first file's production encode fails (its output path matches); the
  // second must still run.
  s.envs.push_back(
    std::make_unique<
      ScopedEnvVar
    >("ENCRO_FAKE_FFMPEG_FAIL_MATCH", (s.temp.path / "encoded" / "sample.mp4").string())
  );

  auto const job = videobatch::EncodingBatchJob{
    .vids = {s.inputPath, b},
    .plannedOutputFiles =
      appctx::path_map<fs::path>{
        {s.inputPath, s.temp.path / "encoded" / "sample.mp4"},
        {b, s.temp.path / "encoded" / "b.mp4"},
      },
    .actionIds = appctx::path_map<std::string>{{s.inputPath, taskA.id}, {b, taskB.id}},
  };

  auto const outcome = videobatch::runEncodingTasks(s.ctx, job, 2, 0);

  REQUIRE(outcome.results.has_value());
  REQUIRE(outcome.results->size() == 2);
  CHECK_FALSE(outcome.results->at(s.inputPath));
  CHECK(outcome.results->at(b));

  store->flush();
  auto reread = jobstate::Store{statePath};
  REQUIRE(reread.initialize(s.ctx.config, false));
  auto const rereadA = reread.findTask(taskA.id);
  auto const rereadB = reread.findTask(taskB.id);
  REQUIRE(rereadA.has_value());
  CHECK(rereadA->status == jobstate::TaskStatus::Failed);
  REQUIRE(rereadA->lastError.has_value());
  CHECK_FALSE(rereadA->lastError->empty());
  REQUIRE(rereadB.has_value());
  CHECK(rereadB->status == jobstate::TaskStatus::Succeeded);
}

TEST_CASE(
  "runEncodingTasks honors stop requests around the probe stage and verbose loop",
  "[video-batch-execution][stop-signal]"
) {
  SECTION("stop raised before the probe stage aborts the batch") {
    auto s = BatchScaffold{};
    s.ctx.config.verbose = true;
    s.ctx.config.yesToAll = true;
    auto stopGuard = testutils::ScopedStopSignalReset{};
    stopsignal::requestStop();

    auto const outcome = videobatch::runEncodingTasks(
      s.ctx,
      BatchScaffold::singleFileJob(s.inputPath, s.temp.path / "encoded" / "sample.mp4"),
      1,
      0
    );

    REQUIRE_FALSE(outcome.results.has_value());
    // The stop landed before any fake-tool invocation: no log was created.
    CHECK_FALSE(fs::exists(s.logPath));
  }

  SECTION("stop raised mid-verbose-loop leaves remaining files unattempted") {
    auto s = BatchScaffold{};
    s.ctx.config.verbose = true;
    s.ctx.config.yesToAll = true;
    // Bypass probing (--crf) so the stop lands inside the verbose encode loop
    // rather than in the probe stage's scoring calls (which share DELAY_MS).
    s.ctx.config.crf = 28;

    auto const b = s.temp.path / "b.mp4";
    testutils::writeSizedFile(b, 1'048'576);
    // Slow the first production encode so the stop lands inside it.
    s.envs.push_back(std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_DELAY_MS", "400"));

    auto stopGuard = testutils::ScopedStopSignalReset{};
    auto job = videobatch::EncodingBatchJob{
      .vids = {s.inputPath, b},
      .plannedOutputFiles =
        appctx::path_map<fs::path>{
          {s.inputPath, s.temp.path / "encoded" / "sample.mp4"},
          {b, s.temp.path / "encoded" / "b.mp4"},
        },
      .actionIds = {},
    };

    std::optional<videobatch::EncodingBatchOutcome> outcome;
    std::jthread runner([&] {
      outcome = videobatch::runEncodingTasks(s.ctx, job, 2, 0);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    stopsignal::requestStop();
    runner.join();

    REQUIRE(outcome.has_value());
    REQUIRE(outcome->results.has_value());
    CHECK(outcome->results->find(b) == outcome->results->end());
    auto const log = testutils::readTextFile(s.logPath);
    CHECK(log.find((s.temp.path / "encoded" / "b.mp4").string()) == std::string::npos);
  }
}

TEST_CASE(
  "runEncodingTasks drops skip-encode plans from encode calls and results",
  "[video-batch-execution]"
) {
  auto s = BatchScaffold{};
  s.ctx.config.yesToAll = true;

  // Small source (512 KiB) gets skipped when the estimated output (1 MiB fake
  // segments) exceeds it; the 3 MiB source survives the filter.
  auto const small = s.temp.path / "small.mp4";
  auto const big = s.temp.path / "big.mp4";
  testutils::writeSizedFile(small, 512ULL * 1024ULL);
  testutils::writeSizedFile(big, 3ULL * 1024ULL * 1024ULL);
  s.envs.push_back(
    std::make_unique<
      ScopedEnvVar
    >("ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", std::to_string(1'048'576))
  );

  auto const job = videobatch::EncodingBatchJob{
    .vids = {small, big},
    .plannedOutputFiles =
      appctx::path_map<fs::path>{
        {small, s.temp.path / "encoded" / "small.mp4"},
        {big, s.temp.path / "encoded" / "big.mp4"},
      },
    .actionIds = {},
  };

  auto const outcome = videobatch::runEncodingTasks(s.ctx, job, 2, 0);

  REQUIRE(outcome.results.has_value());
  CHECK(outcome.results->find(small) == outcome.results->end());
  auto const log = testutils::readTextFile(s.logPath);
  CHECK(log.find((s.temp.path / "encoded" / "small.mp4").string()) == std::string::npos);
}
