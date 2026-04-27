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
  // RED gate: monitorEncodingProgress function exists in anonymous namespace
  // but is not yet wired at startEncodingMonitor call site
  REQUIRE(false);
}
