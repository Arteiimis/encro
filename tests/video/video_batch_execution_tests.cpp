#include "video/video_batch_execution.h"

#include "core/job_state.h"
#include "infra/stop_signal.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

using testutils::ScopedEnvVar;

TEST_CASE(
  "persistedElapsedMs reads accumulated time from the job store",
  "[video-batch-execution]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  testutils::writeTextFile(inputPath);

  auto config = appctx::AppConfig{};
  config.processType = "video";
  config.outputFormat = "mp4";
  config.inputPath = inputPath;
  config.stateFilePath = statePath;

  auto store = jobstate::Store{statePath};
  REQUIRE(store.initialize(config, false));
  auto const task = jobstate::makeEncodeTask(inputPath, outputPath);
  store.mergeTasks(std::array{task});
  CHECK(videobatch::detail::persistedElapsedMs(store, task.id).count() == 0);

  {
    auto clock = testutils::ScopedSyntheticJobClock{1'000'000};
    store.markRunning(task.id);
    clock.advanceMs(25);
    store.markInterrupted(task.id);
  }
  CHECK(videobatch::detail::persistedElapsedMs(store, task.id).count() == 25);
  CHECK(videobatch::detail::persistedElapsedMs(store, std::nullopt).count() == 0);
}

TEST_CASE(
  "barEncodingStart seeds the elapsed clock with the persisted base",
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

  auto state = appctx::EncodingState{};
  state.barIndex = progressState.slots.barIndexes.at(0);
  execCtx.barEncodingStart(state, "clip.mp4", std::chrono::milliseconds{90'000});

  // A progress sample anchors the clock; the elapsed view shows the 90 s
  // base (plus sub-second epsilon) while the estimate is still seeding.
  execCtx.progress().setProgress(state.barIndex.value(), 20.0f);
  auto const elapsed =
    execCtx.progress()
      .elapsedSeconds(state.barIndex.value(), std::chrono::steady_clock::now());
  REQUIRE(elapsed.has_value());
  CHECK(*elapsed >= 90.0f);
  CHECK(*elapsed < 95.0f);
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
  TempDir temp;
  auto const progressFile = temp.path / "progress.log";
  {
    std::ofstream out{progressFile};
    out << "frame=7\nprogress=continue\n";
  }
  auto state = std::make_shared<appctx::EncodingState>();
  state->inputPath = temp.path / "in.mp4";
  state->totalFrames = 100;
  state->progressFilePath = progressFile;
  execCtx.setActive(0, state);

  auto monitor = videobatch::detail::startEncodingMonitor(execCtx);
  // The monitor is provably inside its tick loop once the first parse
  // landed, so the stop below cannot race the monitor's startup. Non-fatal
  // until join: a failed wait must not unwind through the monitor thread.
  auto const monitorReady = testutils::waitUntil(
    [&] {
      auto lock = std::scoped_lock{state->mtx};
      return state->lastFrameCount.value_or(0) == 7;
    },
    std::chrono::seconds{5}
  );

  auto const start = std::chrono::steady_clock::now();
  stopsignal::requestStop();
  // The monitor's stop path requires no active tasks (see the stat-skip
  // case): release the slot before joining or the join never returns.
  execCtx.clearActive(0);
  // Join is the completion detector: the monitor must leave its tick loop on
  // the next wake (event wait wakes it immediately). Hang guard only.
  monitor.join();
  REQUIRE(monitorReady);
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds{30});
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
  // Wait for proof the first parse pass observed frame 10, then rewrite the
  // file with the same size. Polling removes the race where a delayed first
  // parse made the rewrite the first observation. Non-fatal until join: a
  // failed wait must not unwind through the monitor thread.
  auto const firstParseObserved = testutils::waitUntil(
    [&] {
      auto lock = std::scoped_lock{state->mtx};
      return state->lastFrameCount.value_or(0) == 10;
    },
    std::chrono::seconds{5}
  );
  {
    std::ofstream out{progressFile};
    out << "frame=99\nprogress=continue\n";
  }
  // Opportunity-to-parse margin: the rewritten file must leave the value
  // unchanged (that is the assertion), so there is nothing to poll — this
  // only gives the monitor a chance to (wrongly) re-parse if the stat-skip
  // broke; sized above the 250 ms parse throttle.
  std::this_thread::sleep_for(
    std::chrono::milliseconds{300}
  );  // sleep-ok: stat-skip margin

  // Stat-skip: same size means unchanged, so the replacement is not re-parsed.
  {
    auto lock = std::scoped_lock{state->mtx};
    CHECK(state->lastFrameCount.value_or(0) == 10);
  }

  stopsignal::requestStop();
  execCtx.clearActive(0);
  monitor.join();
  REQUIRE(firstParseObserved);
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
    // Input signal cadence: one append per 40 ms drives the throttle;
    // the assertions below are wide bounds on the parse count.
    std::this_thread::sleep_for(
      std::chrono::milliseconds{40}
    );  // sleep-ok: throttle input cadence
    {
      auto lock = std::scoped_lock{state->mtx};
      auto const frame = state->lastFrameCount;
      if (frame.has_value() && frame != lastSeen) {
        lastSeen = frame;
        seen.push_back(frame.value());
      }
    }
  }
  // Poll for the final value instead of a fixed trailing wait. Non-fatal
  // until join: a failed wait must not unwind through the monitor thread.
  auto const finalObserved = testutils::waitUntil(
    [&] {
      auto lock = std::scoped_lock{state->mtx};
      return state->lastFrameCount.value_or(0) == 15;
    },
    std::chrono::seconds{5}
  );
  stopsignal::requestStop();
  execCtx.clearActive(0);
  monitor.join();
  REQUIRE(finalObserved);
  {
    auto lock = std::scoped_lock{state->mtx};
    CHECK(state->lastFrameCount.value_or(0) == 15);  // final value correct
  }
  // Distinct frame values observed across the window: a handful, not one per
  // append.
  CHECK(seen.size() >= 1);
  CHECK(seen.size() < 8);
}
TEST_CASE("barDone state transitions do not throw", "[video-batch-execution]") {
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

  SECTION("success path does not throw") {
    REQUIRE_NOTHROW(execCtx.barDone(barIdx, true, "test.mp4"));
  }

  SECTION("failure path does not throw") {
    REQUIRE_NOTHROW(execCtx.barDone(barIdx, false, "test.mp4"));
  }

  SECTION("nullopt barIndex is a no-op for both outcomes") {
    REQUIRE_NOTHROW(execCtx.barDone(std::nullopt, true, "test.mp4"));
    REQUIRE_NOTHROW(execCtx.barDone(std::nullopt, false, "test.mp4"));
  }
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

// Polls until the fake-tool invocation log contains the needle, giving
// load-independent proof that an invocation started; false after the
// deadline (the REQUIRE surfaces it, no silent pass). Raw ifstream
// predicate: the log does not exist until the first invocation, and
// testutils::readTextFile asserts on unopenable files.
auto waitUntilLogContains(fs::path const& logPath, std::string_view needle) -> bool {
  return testutils::waitUntil(
    [&] {
      auto log = std::ifstream{logPath, std::ios::binary};
      if (!log.is_open()) { return false; }
      auto const content = std::string{std::istreambuf_iterator<char>{log}, {}};
      return content.find(needle) != std::string::npos;
    },
    std::chrono::seconds{10},
    std::chrono::milliseconds{10}
  );
}

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
    // The abort contract is the nullopt results: whether the worker observed
    // the stop before or after spawning the first fake-tool invocation is a
    // load-dependent timing detail (the child's log may exist), not a
    // correctness claim — so the log's absence is not asserted here.
  }

  SECTION("stop raised mid-verbose-loop leaves remaining files unattempted") {
    auto s = BatchScaffold{};
    s.ctx.config.verbose = true;
    s.ctx.config.yesToAll = true;
    // Bypass probing (--crf) so the stop lands inside the verbose encode loop
    // rather than in the probe stage's scoring calls.
    s.ctx.config.crf = 28;

    auto const b = s.temp.path / "b.mp4";
    testutils::writeSizedFile(b, 1'048'576);
    // Hold each encode start on a gate file: the fake tool logs the
    // invocation and then blocks, so the first "ffmpeg" log line is proof
    // the encode is in flight and the stop request cannot race ahead of it.
    auto const gateFile = s.temp.path / "encode-gate";
    s.envs.push_back(
      std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_GATE_FILE", gateFile.string())
    );

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
    REQUIRE(waitUntilLogContains(s.logPath, "ffmpeg\t"));
    stopsignal::requestStop();
    {
      auto gate = std::ofstream{gateFile, std::ios::binary | std::ios::trunc};
      REQUIRE(gate.is_open());
      gate << "go";
    }
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
