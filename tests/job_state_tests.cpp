#include "core/job_state.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <array>
#include <filesystem>

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
