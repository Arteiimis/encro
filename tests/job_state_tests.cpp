#include "core/job_state.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <array>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

void writeFile(fs::path const& path, std::string_view content = "x") {
  std::ofstream out{path, std::ios::binary};
  REQUIRE(out.is_open());
  out << content;
}

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
  auto const action = jobstate::makeEncodeAction(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  auto merged = store.mergeActions(std::array{action});
  REQUIRE(merged.size() == 1);
  store.markRunning(action.id);
  store.markSucceeded(action.id);
  store.flush();

  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  CHECK(resumeRes.value());

  auto const resumed = resumedStore.mergeActions(std::array{action});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::ActionStatus::Succeeded);
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
  auto const action = jobstate::makeEncodeAction(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeActions(std::array{action});
  store.markRunning(action.id);
  store.markSucceeded(action.id);
  store.flush();

  fs::remove(outputPath);

  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeActions(std::array{action});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::ActionStatus::Pending);
}

TEST_CASE("job state turns running actions into interrupted on resume", "[job-state]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputPath = temp.path / "input.hevc.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);

  auto const config = makeConfig(inputPath, statePath);
  auto const action = jobstate::makeEncodeAction(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeActions(std::array{action});
  store.markRunning(action.id);
  store.flush();

  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeActions(std::array{action});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::ActionStatus::Interrupted);
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
  auto const action = jobstate::makeEncodeAction(inputPath, outputPath);

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(config, false);
  REQUIRE(initRes);
  store.mergeActions(std::array{action});
  store.markInterrupted(action.id, "canceled by user");
  store.flush();

  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(config, false);
  REQUIRE(resumeRes);
  auto const resumed = resumedStore.mergeActions(std::array{action});
  REQUIRE(resumed.size() == 1);
  CHECK(resumed.front().status == jobstate::ActionStatus::Succeeded);
  REQUIRE(resumed.front().lastProgress.has_value());
  CHECK(resumed.front().lastProgress.value() == Catch::Approx(100.0f));
}
