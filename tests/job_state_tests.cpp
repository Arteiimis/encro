#include "core/collision_naming.h"
#include "core/job_state.h"
#include "logging/log_tags.h"
#include "logging/setup.h"
#include "test_utils.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <catch2/catch_all.hpp>

#include <array>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ponytail: duplicate of the helper in tests/logging_infra_test.cpp; hoist to
// test_utils.h when a fifth copy appears.
namespace fs = std::filesystem;
using testutils::writeFile;

namespace {

auto makeConfig(fs::path const& inputPath, fs::path const& statePath)
  -> appctx::AppConfig {
  auto config = appctx::AppConfig{};
  config.processType = "video";
  config.outputFormat = "mp4";
  config.inputPath = inputPath;
  config.stateFilePath = statePath;
  return config;
}

}  // namespace

TEST_CASE("compress phase marker task has stable id and kind", "[job-state]") {
  auto const task = jobstate::makeCompressPhaseTask();
  CHECK(task.id == jobstate::kCompressPhaseTaskId);
  CHECK(task.kind == jobstate::kCompressPhaseKind);
  CHECK(task.status == jobstate::TaskStatus::Pending);
}

TEST_CASE("job state keeps succeeded encode action when output exists", "[job-state]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);
  writeFile(outputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto const task = jobstate::makeEncodeTask(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  auto merged = store.mergeTasks(std::array{task});
  REQUIRE(merged.size() == 1);
  store.markRunning(task.id);
  store.markSucceeded(task.id);
  store.flush();

  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  CHECK(resumeRes.value());

  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Succeeded);
}

TEST_CASE(
  "job state resets succeeded encode action when output is missing",
  "[job-state]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);
  writeFile(outputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto const task = jobstate::makeEncodeTask(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{task});
  store.markRunning(task.id);
  store.markSucceeded(task.id);
  store.flush();

  fs::remove(outputPath);

  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Pending);
}

TEST_CASE("job state turns running actions into interrupted on resume", "[job-state]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto const task = jobstate::makeEncodeTask(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{task});
  store.markRunning(task.id);
  store.flush();

  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Interrupted);
}

TEST_CASE(
  "job state restores interrupted encode action when output already exists",
  "[job-state]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);
  writeFile(outputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto const task = jobstate::makeEncodeTask(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{task});
  store.markInterrupted(task.id, "canceled by user");
  store.flush();

  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Succeeded);
  REQUIRE(resumed.front().lastProgress.has_value());
  CHECK(resumed.front().lastProgress.value() == Catch::Approx(100.0f));
}

TEST_CASE("job state resets encode action when planned target changes", "[job-state]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const oldOutputPath = temp.path / "old.hevc.mp4";
  auto const newOutputPath = temp.path / "new.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);
  writeFile(oldOutputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto const oldTask = jobstate::makeEncodeTask(inputPath, oldOutputPath);
  auto const newTask = jobstate::makeEncodeTask(inputPath, newOutputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{oldTask});
  store.markRunning(oldTask.id);
  store.markSucceeded(oldTask.id);
  store.flush();
  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeTasks(std::array{newTask});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Pending);
  REQUIRE(resumed.front().targetPaths.size() == 1);
  CHECK(resumed.front().targetPaths.front() == newOutputPath);
}

TEST_CASE("job state round-trips segment fields through JSON", "[job-state]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto task = jobstate::makeEncodeTask(inputPath, outputPath);
  task.segmentIndex = 3;
  task.resumeTimeUs = 30'000'000;

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{task});
  store.markInterrupted(task.id);
  store.flush();
  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  REQUIRE(resumed.front().segmentIndex.has_value());
  CHECK(resumed.front().segmentIndex.value() == 3);
  REQUIRE(resumed.front().resumeTimeUs.has_value());
  CHECK(resumed.front().resumeTimeUs.value() == 30'000'000);
}

TEST_CASE("job state keeps absent segment fields as nullopt", "[job-state]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto const task = jobstate::makeEncodeTask(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{task});
  store.markInterrupted(task.id);
  store.flush();
  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  CHECK_FALSE(resumed.front().segmentIndex.has_value());
  CHECK_FALSE(resumed.front().resumeTimeUs.has_value());
}

TEST_CASE("job state markSegmentProgress persists segment fields", "[job-state]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto const task = jobstate::makeEncodeTask(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{task});
  store.markSegmentProgress(task.id, 4, 40'000'000);
  store.flush();
  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  REQUIRE(resumed.front().segmentIndex.has_value());
  CHECK(resumed.front().segmentIndex.value() == 4);
  REQUIRE(resumed.front().resumeTimeUs.has_value());
  CHECK(resumed.front().resumeTimeUs.value() == 40'000'000);
}

TEST_CASE(
  "job state does not restore segmented task from partial output",
  "[job-state]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);
  writeFile(outputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto task = jobstate::makeEncodeTask(inputPath, outputPath);
  task.segmentIndex = 5;
  task.resumeTimeUs = 50'000'000;

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{task});
  store.markInterrupted(task.id);
  store.flush();
  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Interrupted);
  REQUIRE(resumed.front().segmentIndex.has_value());
  CHECK(resumed.front().segmentIndex.value() == 5);
}

TEST_CASE(
  "job state keeps segment records for concat-only rerun when output is missing",
  "[job-state]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto task = jobstate::makeEncodeTask(inputPath, outputPath);
  task.segmentIndex = 3;
  task.resumeTimeUs = 30'000'000;

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{task});
  store.markInterrupted(task.id);
  store.flush();
  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Interrupted);
  REQUIRE(resumed.front().resumeTimeUs.has_value());
  CHECK(resumed.front().resumeTimeUs.value() == 30'000'000);
}

TEST_CASE("job state keeps segmented succeeded task when output exists", "[job-state]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);
  writeFile(outputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto task = jobstate::makeEncodeTask(inputPath, outputPath);
  task.segmentIndex = 2;
  task.resumeTimeUs = 20'000'000;

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{task});
  store.markRunning(task.id);
  store.markSucceeded(task.id);
  store.flush();

  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Succeeded);
}

TEST_CASE("job state clears segment fields when planned target changes", "[job-state]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const oldOutputPath = temp.path / "old.hevc.mp4";
  auto const newOutputPath = temp.path / "new.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);
  writeFile(oldOutputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto oldTask = jobstate::makeEncodeTask(inputPath, oldOutputPath);
  oldTask.segmentIndex = 2;
  oldTask.resumeTimeUs = 20'000'000;
  auto const newTask = jobstate::makeEncodeTask(inputPath, newOutputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{oldTask});
  store.markRunning(oldTask.id);
  store.markSucceeded(oldTask.id);
  store.flush();
  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeTasks(std::array{newTask});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Pending);
  CHECK_FALSE(resumed.front().segmentIndex.has_value());
  CHECK_FALSE(resumed.front().resumeTimeUs.has_value());
}

TEST_CASE("job state resets archive action when member set changes", "[job-state]") {
  TempDir temp;
  auto const inputPath = temp.path / "input";
  auto const zipPath = temp.path / "bundle.zip";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const memberA = temp.path / "a.bin";
  auto const memberB = temp.path / "b.bin";
  auto const memberC = temp.path / "c.bin";
  fs::create_directories(inputPath);
  writeFile(memberA);
  writeFile(memberB);
  writeFile(memberC);
  writeFile(zipPath);

  auto config = makeConfig(inputPath, statePath);
  config.processType = "picture";

  auto const oldTask =
    jobstate::makeArchiveTask(zipPath, std::array{memberA, memberB}, "bundle");
  auto const newTask =
    jobstate::makeArchiveTask(zipPath, std::array{memberA, memberC}, "bundle");

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{oldTask});
  store.markRunning(oldTask.id);
  store.markSucceeded(oldTask.id);
  store.flush();
  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeTasks(std::array{newTask});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Pending);
  REQUIRE(resumed.front().sourcePaths.size() == 2);
  CHECK(resumed.front().sourcePaths[0] == memberA);
  CHECK(resumed.front().sourcePaths[1] == memberC);
}

TEST_CASE(
  "job state matches pack-enabled run against encode-only saved state",
  "[job-state]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);
  writeFile(outputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto const task = jobstate::makeEncodeTask(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{task});
  store.markRunning(task.id);
  store.markSucceeded(task.id);
  store.flush();

  auto packConfig = makeConfig(inputPath, statePath);
  packConfig.packOutput = true;

  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(packConfig, false);
  REQUIRE(resumeRes);
  CHECK(resumeRes.value());

  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Succeeded);
}

TEST_CASE(
  "job state rejects encode-only run against pack-enabled saved state",
  "[job-state]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);
  writeFile(outputPath);

  auto packConfig = makeConfig(inputPath, statePath);
  packConfig.packOutput = true;
  auto const task = jobstate::makeEncodeTask(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(packConfig, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{task});
  store.markRunning(task.id);
  store.markSucceeded(task.id);
  store.flush();

  auto const config = makeConfig(inputPath, statePath);

  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  CHECK_FALSE(resumeRes.value());

  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Pending);
}

TEST_CASE(
  "job state rejects pack-enabled run when another config field differs",
  "[job-state]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);
  writeFile(outputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto const task = jobstate::makeEncodeTask(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{task});
  store.markRunning(task.id);
  store.markSucceeded(task.id);
  store.flush();

  auto changedConfig = makeConfig(inputPath, statePath);
  changedConfig.packOutput = true;
  changedConfig.outputFormat = "webp";

  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(changedConfig, false);
  REQUIRE(resumeRes);
  CHECK_FALSE(resumeRes.value());

  auto const resumed = resumedStore.mergeTasks(std::array{task});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::TaskStatus::Pending);
}

TEST_CASE(
  "job state reports discarded mismatch when auto-resume config differs",
  "[job-state]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.flush();
  auto changedConfig = makeConfig(inputPath, statePath);
  changedConfig.outputFormat = "webp";

  auto resumedStore = jobstate::Store{statePath};
  auto discardedMismatched = false;
  auto const resumeRes =
    resumedStore.initialize(changedConfig, false, &discardedMismatched);
  REQUIRE(resumeRes);
  CHECK_FALSE(resumeRes.value());
  CHECK(discardedMismatched);
}

TEST_CASE(
  "job state does not report discarded mismatch without a state file",
  "[job-state]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto store = jobstate::Store{statePath};
  auto discardedMismatched = true;
  auto const initRes = store.initialize(config, false, &discardedMismatched);
  REQUIRE(initRes);
  CHECK_FALSE(initRes.value());
  CHECK_FALSE(discardedMismatched);
}

TEST_CASE(
  "job state does not discard or report mismatch on explicit resume error",
  "[job-state]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.flush();
  auto changedConfig = makeConfig(inputPath, statePath);
  changedConfig.outputFormat = "webp";
  changedConfig.resumeState = true;

  auto resumedStore = jobstate::Store{statePath};
  auto discardedMismatched = true;
  auto const resumeRes =
    resumedStore.initialize(changedConfig, false, &discardedMismatched);
  REQUIRE_FALSE(resumeRes);
  CHECK(resumeRes.error().find("does not match") != std::string::npos);
  CHECK_FALSE(discardedMismatched);
}

TEST_CASE(
  "job state initialize fails when the state file cannot be written",
  "[job-state]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  writeFile(inputPath);

  // Make the parent path a regular file so the state temp file cannot be created.
  auto const blockerFile = temp.path / "blocker";
  writeFile(blockerFile);
  auto const statePath = blockerFile / "encro.job-state.json";

  auto const config = makeConfig(inputPath, statePath);
  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);

  // Initial persistence failure must surface, not silently succeed.
  REQUIRE_FALSE(initRes);
}

TEST_CASE("job state mark operations report persistence failures", "[job-state]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  writeFile(inputPath);
  auto const statePath = temp.path / "encro.job-state.json";

  auto const config = makeConfig(inputPath, statePath);
  auto const task = jobstate::makeEncodeTask(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeTasks(std::array{task});

  // Replace the state file with a non-empty directory: the temp file still
  // writes, but the final rename cannot succeed (remove of the non-empty
  // directory fails, so both fallback attempts fail).
  fs::remove(statePath);
  fs::create_directory(statePath);
  writeFile(statePath / "keep.txt");

  // Best-effort mutators report persistence failures through the log.
  auto const [logger, log] = testutils::registerCapturingLogger(logtags::CORE_JOB);
  store.markRunning(task.id);
  store.flush();
  auto const content = log->str();
  CHECK(
    content.find("Failed to persist job state after markRunning") != std::string::npos
  );
  CHECK(content.find("Failed to persist job state after flush") != std::string::npos);
  spdlog::drop(logtags::CORE_JOB);
}

// ── RED 2.3 — fresh state adopts the logging run id as jobId ────────────────

TEST_CASE("fresh job state adopts logging run id as jobId", "[job-state][run_id]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);
  auto const config = makeConfig(inputPath, statePath);

  logging::setRunId("expected-run-id-123");

  auto store = jobstate::Store{statePath};
  REQUIRE(store.initialize(config, false));
  CHECK(store.currentJobId() == "expected-run-id-123");
}

// ── RED 2.5 — resume adopts the persisted jobId as the run id ───────────────

TEST_CASE("resume adopts persisted jobId as logging run id", "[job-state][run_id]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);
  auto const config = makeConfig(inputPath, statePath);

  logging::setRunId("first-run-id");
  auto firstStore = jobstate::Store{statePath};
  REQUIRE(firstStore.initialize(config, false));
  auto const persistedJobId = firstStore.currentJobId();
  REQUIRE_FALSE(persistedJobId.empty());

  // Second invocation: different bootstrap id, same state file
  logging::setRunId("second-run-id");
  auto resumedStore = jobstate::Store{statePath};
  REQUIRE(resumedStore.initialize(config, false));

  CHECK(logging::runId() == persistedJobId);
}

// ── 3.6 — task ids use the same normalized form as job-state records ────────

TEST_CASE("encode and archive task ids use normalized path form", "[job-state]") {
  auto const input = fs::path{R"(C:\vids\a.mkv)"};
  auto const output = fs::path{R"(C:\vids\a.hevc.mp4)"};
  auto const zip = fs::path{R"(C:\out\b.zip)"};

  auto const encodeTask = jobstate::makeEncodeTask(input, output);
  CHECK(
    encodeTask.id == std::format("encode:{}", collisionnaming::stablePathString(input))
  );

  auto const archiveTask = jobstate::makeArchiveTask(zip, std::array{input}, "label");
  CHECK(
    archiveTask.id == std::format("archive:{}", collisionnaming::stablePathString(zip))
  );
}
