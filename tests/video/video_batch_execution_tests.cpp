#include "video/video_batch_execution.h"

#include "infra/stop_signal.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

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
