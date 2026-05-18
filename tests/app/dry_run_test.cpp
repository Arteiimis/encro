#include "app/pipeline.h"
#include "core/app_context.h"
#include "core/job_state.h"
#include "core/job_state_detail.h"
#include "core/error_handle.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using testutils::writeFile;

namespace {

// AppContext can't be copied (immer::atom has deleted copy ctor),
// so we use a setup function that takes a reference.
void setupVideoContext(appctx::AppContext& ctx, fs::path const& inputPath) {
  ctx.config.inputPath = inputPath;
  ctx.config.outputFormat = "mp4";
  ctx.config.processType = "video";
  ctx.toolchain.ffmpegPath = fs::path{"/usr/bin/ffmpeg"};
  ctx.toolchain.ffprobePath = fs::path{"/usr/bin/ffprobe"};
}

void setupPackOnlyContext(appctx::AppContext& ctx, fs::path const& inputPath) {
  setupVideoContext(ctx, inputPath);
  ctx.config.packOnly = true;
  ctx.toolchain.ffmpegPath = std::nullopt;
  ctx.toolchain.ffprobePath = std::nullopt;
}

}  // namespace

TEST_CASE("runDryRun with valid input completes all layers and returns 0", "[dry-run]") {
  TempDir temp;
  auto const inputDir = temp.path / "media";
  fs::create_directories(inputDir);
  writeFile(inputDir / "video.mp4", "fake mp4 content");
  writeFile(inputDir / "clip.mkv", "fake mkv content");

  auto ctx = appctx::AppContext{};
  setupVideoContext(ctx, inputDir);
  ctx.config.recursive = true;

  auto result = pipeline::runDryRun(ctx);
  REQUIRE(result.has_value());
  CHECK(result.value() == 0);
}

TEST_CASE("runDryRun with nonexistent input path returns error", "[dry-run]") {
  auto ctx = appctx::AppContext{};
  setupVideoContext(ctx, fs::path{"/nonexistent/path/12345"});
  auto result = pipeline::runDryRun(ctx);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("runDryRun with --pack-only skips ffmpeg/ffprobe check and succeeds", "[dry-run]") {
  TempDir temp;
  auto const inputDir = temp.path / "media";
  fs::create_directories(inputDir);
  writeFile(inputDir / "data.bin", "content");

  auto ctx = appctx::AppContext{};
  setupPackOnlyContext(ctx, inputDir);
  auto result = pipeline::runDryRun(ctx);
  REQUIRE(result.has_value());
  CHECK(result.value() == 0);
}

TEST_CASE("runDryRun with --pack-only and nonexistent input returns error", "[dry-run]") {
  auto ctx = appctx::AppContext{};
  setupPackOnlyContext(ctx, fs::path{"/nonexistent/path/67890"});
  auto result = pipeline::runDryRun(ctx);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("dry-run does not create output directories", "[dry-run]") {
  TempDir temp;
  auto const inputDir = temp.path / "media";
  fs::create_directories(inputDir);
  writeFile(inputDir / "video.mp4", "fake mp4 content");

  auto const outputDir = temp.path / "expected_output";

  auto ctx = appctx::AppContext{};
  setupVideoContext(ctx, inputDir);
  ctx.config.outputPath = outputDir;

  auto result = pipeline::runDryRun(ctx);
  REQUIRE(result.has_value());

  CHECK_FALSE(fs::exists(outputDir));
}

TEST_CASE("dry-run does not create job state file", "[dry-run]") {
  TempDir temp;
  auto const inputDir = temp.path / "media";
  fs::create_directories(inputDir);
  writeFile(inputDir / "video.mp4", "fake mp4 content");

  auto const stateFile = temp.path / "state.json";

  auto ctx = appctx::AppContext{};
  setupVideoContext(ctx, inputDir);
  ctx.config.stateFilePath = stateFile;
  ctx.config.resumeState = true;

  auto result = pipeline::runDryRun(ctx);
  REQUIRE(result.has_value());

  CHECK_FALSE(fs::exists(stateFile));
}

TEST_CASE("runDryRun with --resume reads existing job state without modifying", "[dry-run]") {
  TempDir temp;
  auto const inputDir = temp.path / "media";
  fs::create_directories(inputDir);
  writeFile(inputDir / "video.mp4", "fake mp4 content");

  auto const stateFile = temp.path / "state.json";
  {
    auto store = jobstate::Store{stateFile};
    std::vector<jobstate::TaskRecord> plannedTasks;
    {
      auto t = jobstate::TaskRecord{};
      t.id = "task-1";
      t.status = jobstate::TaskStatus::Succeeded;
      plannedTasks.push_back(t);
    }
    {
      auto t = jobstate::TaskRecord{};
      t.id = "task-2";
      t.status = jobstate::TaskStatus::Succeeded;
      plannedTasks.push_back(t);
    }
    {
      auto t = jobstate::TaskRecord{};
      t.id = "task-3";
      t.status = jobstate::TaskStatus::Pending;
      plannedTasks.push_back(t);
    }
    // store.initialize creates the job state file
    {
      auto initCtx = appctx::AppContext{};
      initCtx.config.inputPath = inputDir;
      initCtx.config.processType = "video";
      auto initRes = store.initialize(initCtx.config, false);
      REQUIRE(initRes.has_value());
    }
    store.mergeTasks(plannedTasks);
    store.flush();
    store.setStage("encoding");
    store.flush();
  }

  REQUIRE(fs::exists(stateFile));

  auto ctx = appctx::AppContext{};
  setupVideoContext(ctx, inputDir);
  ctx.config.stateFilePath = stateFile;
  ctx.config.resumeState = true;

  auto result = pipeline::runDryRun(ctx);
  REQUIRE(result.has_value());
  CHECK(result.value() == 0);

  CHECK(fs::exists(stateFile));
}

TEST_CASE("runDryRun with --resume and no job state file handles gracefully", "[dry-run]") {
  TempDir temp;
  auto const inputDir = temp.path / "media";
  fs::create_directories(inputDir);
  writeFile(inputDir / "video.mp4", "fake mp4 content");

  auto const stateFile = temp.path / "nonexistent_state.json";

  auto ctx = appctx::AppContext{};
  setupVideoContext(ctx, inputDir);
  ctx.config.stateFilePath = stateFile;
  ctx.config.resumeState = true;

  auto result = pipeline::runDryRun(ctx);
  REQUIRE(result.has_value());
  CHECK(result.value() == 0);

  CHECK_FALSE(fs::exists(stateFile));
}
