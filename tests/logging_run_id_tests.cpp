#include "logging/setup.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <string>

// ── RED 2.1 — runId() lifecycle ─────────────────────────────────────────────

TEST_CASE(
  "runId() lifecycle: stable, replaceable, reset by shutdown",
  "[logging][run_id]"
) {
  auto const first = logging::runId();
  auto const second = logging::runId();

  CHECK_FALSE(first.empty());
  CHECK(first == second);

  logging::setRunId("test-run-42");

  CHECK(logging::runId() == "test-run-42");

  logging::setRunId("pre-shutdown-id");
  logging::shutdown();

  // After shutdown the next query lazily regenerates a fresh id
  auto const fresh = logging::runId();
  CHECK_FALSE(fresh.empty());
  CHECK(fresh != "pre-shutdown-id");
}

// ── RED 7.1 — lock-free snapshot for the crash handler ─────────────────────

TEST_CASE(
  "runIdSnapshot() mirrors the run id and clears on shutdown",
  "[logging][run_id]"
) {
  logging::setRunId("snapshot-id-1");
  CHECK(logging::runIdSnapshot() == "snapshot-id-1");

  logging::setRunId("snapshot-id-2");
  CHECK(logging::runIdSnapshot() == "snapshot-id-2");

  logging::setRunId("snapshot-before-shutdown");
  logging::shutdown();
  CHECK(logging::runIdSnapshot().empty());
}
