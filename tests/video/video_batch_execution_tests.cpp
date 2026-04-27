#include "video/video_batch_execution.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("videobatch types compile and are usable", "[video-batch-execution]") {
  // RED phase: test deliberately fails to signal extraction-in-progress.
  // Extracted functions (reportEncodingStatus, markRunningNoProgress,
  // finalizeEncodeResult) will be verified in GREEN phase.
  REQUIRE(false);
}

TEST_CASE("ActionIdMap and EncodeResultsMap are defined", "[video-batch-execution]") {
  // Verify the public types exist and compile
  auto actionIds = videobatch::ActionIdMap{};
  auto results = videobatch::EncodeResultsMap{};
  CHECK(actionIds.size() == 0);
  CHECK(results.size() == 0);
}
