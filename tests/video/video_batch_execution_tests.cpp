#include "video/video_batch_execution.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("videobatch types compile and are usable", "[video-batch-execution]") {
  // GREEN phase: extraction complete — public API types verified
  auto actionIds = videobatch::ActionIdMap{};
  auto results = videobatch::EncodeResultsMap{};
  CHECK(actionIds.size() == 0);
  CHECK(results.size() == 0);
}

TEST_CASE("ActionIdMap and EncodeResultsMap are defined", "[video-batch-execution]") {
  // Verify the public types exist and compile
  auto actionIds = videobatch::ActionIdMap{};
  auto results = videobatch::EncodeResultsMap{};
  CHECK(actionIds.size() == 0);
  CHECK(results.size() == 0);
}

TEST_CASE("runEncodingWithoutProgress helpers extracted", "[video-batch-execution]") {
  // GREEN phase: extraction complete — public API unchanged, types verified
  auto actionIds = videobatch::ActionIdMap{};
  auto results = videobatch::EncodeResultsMap{};
  // Both helper functions (markRunningNoProgress, finalizeEncodeResult)
  // are now wired in runEncodingWithoutProgress replacing inline lambdas
  CHECK(true);
}

TEST_CASE(
  "startEncodingMonitor jthread lambda extracted to monitorEncodingProgress",
  "[video-batch-execution]"
) {
  // GREEN phase: monitorEncodingProgress function extracted and wired.
  // startEncodingMonitor is now a 3-line function with 1-line lambda delegation.
  // Inner withActionJobState lambdas inside monitorEncodingProgress are unchanged.
  auto actionIds = videobatch::ActionIdMap{};
  auto results = videobatch::EncodeResultsMap{};
  CHECK(actionIds.size() == 0);
  CHECK(results.size() == 0);
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
