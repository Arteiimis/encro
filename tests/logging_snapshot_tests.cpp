#include "logging/log_tags.h"
#include "logging/logging.h"
#include "logging/setup.h"
#include "core/app_context.h"
#include "test_utils.h"

#include <spdlog/logger.h>  // IWYU pragma: keep -- needed with libstdc++; MSVC pulls it transitively

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <memory>  // IWYU pragma: keep -- needed with libstdc++; MSVC pulls it transitively
#include <sstream>
#include <string>

namespace {

// ── Helper: RAII guard that clears forensic state before and after test ──

struct ScopedForensicReset {
  ScopedForensicReset() {
    logging::detail::resetContextStack();
    logging::clearForensicSnapshotData();
  }
  ~ScopedForensicReset() {
    logging::detail::resetContextStack();
    logging::clearForensicSnapshotData();
  }
  ScopedForensicReset(ScopedForensicReset const&) = delete;
  auto operator=(ScopedForensicReset const&) -> ScopedForensicReset& = delete;
};

}  // namespace

// ── File-scoped DEFINE_LOGGER ──
DEFINE_LOGGER(logtags::TEST_INFRA);

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — captureEnvironmentSnapshot() reflects the forensic state
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(
  "captureEnvironmentSnapshot reflects the forensic state",
  "[logging][snapshot]"
) {
  ScopedForensicReset reset;

  SECTION("empty when no context is set") {
    auto const snapshot = logging::captureEnvironmentSnapshot();
    CAPTURE(snapshot);
    CHECK(snapshot.empty());
    CHECK(snapshot == "");
  }

  SECTION("non-empty when AppContext is set") {
    appctx::AppContext mockCtx{};
    mockCtx.config.processType = "video";

    logging::setForensicAppContext(&mockCtx);
    auto const snapshot = logging::captureEnvironmentSnapshot();
    CAPTURE(snapshot);
    CHECK_FALSE(snapshot.empty());
    CHECK(snapshot.find("Environment:") != std::string::npos);
  }

  SECTION("contains required fields when encoding active") {
    appctx::AppContext mockCtx{};
    mockCtx.config.processType = "video";

    logging::setForensicAppContext(&mockCtx);

    logging::EnvironmentSnapshot data{};
    data.hasEncodingContext = true;
    data.pipelineType = "video";
    data.activeSlots = 3;
    data.totalSlots = 8;
    data.pending = 12;
    data.finished = 45;
    data.subprocessPid = 28476;
    data.subprocessCmdline = "ffmpeg -i input.mkv -c:v libx264 output.mp4";

    logging::setForensicSnapshotData(data);

    auto const snapshot = logging::captureEnvironmentSnapshot();
    CAPTURE(snapshot);

    CHECK(snapshot.find("active-slots=") != std::string::npos);
    CHECK(snapshot.find("pending=") != std::string::npos);
    CHECK(snapshot.find("subprocess=") != std::string::npos);
    CHECK(snapshot.find("3/8") != std::string::npos);
    CHECK(snapshot.find("12") != std::string::npos);
    CHECK(snapshot.find("28476") != std::string::npos);
  }

  SECTION("safe with null encoding context") {
    appctx::AppContext mockCtx{};
    mockCtx.config.processType = "picture";

    logging::setForensicAppContext(&mockCtx);

    // No exec context set — snapshot should indicate no encoding active
    auto const snapshot = logging::captureEnvironmentSnapshot();
    CAPTURE(snapshot);

    CHECK_FALSE(snapshot.empty());
    CHECK(snapshot.find("Environment:") != std::string::npos);
    CHECK(snapshot.find("no encoding") != std::string::npos);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5 — Snapshot emitted after LOG_ERROR with context chain
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Snapshot emitted after LOG_ERROR", "[logging][snapshot]") {
  ScopedForensicReset reset;
  auto [logger, oss] = testutils::registerCapturingLogger(logtags::TEST_INFRA);

  appctx::AppContext mockCtx{};
  mockCtx.config.processType = "video";

  logging::setForensicAppContext(&mockCtx);

  logging::EnvironmentSnapshot data{};
  data.hasEncodingContext = true;
  data.pipelineType = "video";
  data.activeSlots = 2;
  data.totalSlots = 4;
  data.pending = 7;
  data.finished = 10;

  logging::setForensicSnapshotData(data);

  {
    logging::ScopedErrorContext ctx("encode", "test.mkv");
    LOG_ERROR("encoding failure");
    logger->flush();
  }

  auto const output = oss->str();
  CAPTURE(output);

  // Error line lands (context-chain attachment itself is covered by the
  // error_context tests)
  CHECK(output.find("encoding failure") != std::string::npos);

  // Should contain the environment snapshot after the error line
  CHECK(output.find("Environment:") != std::string::npos);
  CHECK(output.find("active-slots=") != std::string::npos);

  // Snapshot should appear after the error line
  auto const errorPos = output.find("encoding failure");
  auto const snapshotPos = output.find("Environment:");
  CHECK(errorPos < snapshotPos);
}
