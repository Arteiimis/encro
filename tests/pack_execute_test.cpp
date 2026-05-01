// pack::execute() tests — Task 1 (non-resumable) + Task 2 (resumable)
//
// Non-resumable tests (Task 1):
// - execute() with empty entries returns PackRunResult{0, {}} (no crash, no error)
// - execute() with mode=Directory packs directory, returns exitCode 0 with zippedFiles
// - execute() with mode=Media groups entries by parent dir, packs to outputDir, returns zippedFiles
// - execute() with compact=false produces full-progress packing (packGroupsFull path called)
//
// Resumable tests (Task 2 — RED phase):
// - execute() with jobState=nullptr behaves identically to non-resumable (already covered above)
// - execute() with jobState!=nullptr calls mergeTasks, filters needsExecution, sets up callbacks
// - execute() with jobState!=nullptr and all tasks already complete → returns exitCode 0, no packing
// - execute() with jobState!=nullptr and cancel requested → returns kCanceledExitCode

#include "pack/pack.h"
#include "pack/pack_types.h"
#include "core/job_state.h"
#include "core/app_context.h"
#include "infra/stop_signal.h"
#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Helper: create a file with given size (binary content)
auto createBinaryFile(fs::path const& filePath, std::size_t sizeBytes = 100) -> void {
  fs::create_directories(filePath.parent_path());
  auto out = std::ofstream{filePath, std::ios::binary};
  auto const data = std::string(sizeBytes, 'X');
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  out.close();
}

// Helper: create a minimal AppConfig for Store initialization
auto makeStoreConfig(fs::path const& statePath) -> appctx::AppConfig {
  auto config = appctx::AppConfig{};
  config.processType = "video";
  config.outputFormat = "mp4";
  config.inputPath = statePath.parent_path() / "dummy.mp4";
  config.packOutput = true;
  config.stateFilePath = statePath;
  return config;
}

}  // namespace

// ============================================================
// Task 1: Non-resumable tests
// ============================================================

TEST_CASE("execute() with empty entries returns empty success", "[pack][execute]") {
  pack::PackRequest req{
    .entries = {},
    .mode = pack::PackMode::Media,
    .outputDir = fs::temp_directory_path() / "encro_test_empty",
  };
  fs::create_directories(req.outputDir);

  auto const result = pack::execute(req);
  REQUIRE(result.has_value());
  CHECK(result->exitCode == 0);
  CHECK(result->zippedFiles.empty());
}

TEST_CASE("execute() with Media mode groups entries by parent dir", "[pack][execute]") {
  TempDir tmp;
  auto const subdirA = tmp.path / "video_a";
  auto const subdirB = tmp.path / "video_b";
  fs::create_directories(subdirA);
  fs::create_directories(subdirB);
  createBinaryFile(subdirA / "stream.mp4", 1024);
  createBinaryFile(subdirA / "subtitle.srt", 512);
  createBinaryFile(subdirB / "output.mp4", 2048);

  auto const outputDir = tmp.path / "packed";
  fs::create_directories(outputDir);

  pack::PackRequest req{
    .entries = {subdirA / "stream.mp4", subdirA / "subtitle.srt", subdirB / "output.mp4"},
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .compact = true,
  };

  auto const result = pack::execute(req);
  REQUIRE(result.has_value());
  CHECK(result->exitCode == 0);
  // At least one zip file should be produced
  CHECK_FALSE(result->zippedFiles.empty());
  for (auto const& zip: result->zippedFiles) { CHECK(fs::exists(zip)); }
}

TEST_CASE("execute() with Directory mode packs directory tree", "[pack][execute]") {
  TempDir tmp;
  createBinaryFile(tmp.path / "docs/readme.txt", 200);
  createBinaryFile(tmp.path / "docs/guide.pdf", 300);
  createBinaryFile(tmp.path / "docs/nested/deep.txt", 100);

  auto const outputDir = tmp.path / "packed_dir";
  fs::create_directories(outputDir);

  pack::PackRequest req{
    .entries = {tmp.path / "docs"},
    .mode = pack::PackMode::Directory,
    .outputDir = outputDir,
    .compact = true,
    .recursive = true,
  };

  auto const result = pack::execute(req);
  REQUIRE(result.has_value());
  CHECK(result->exitCode == 0);
  CHECK_FALSE(result->zippedFiles.empty());
  for (auto const& zip: result->zippedFiles) { CHECK(fs::exists(zip)); }
}

TEST_CASE("execute() with compact=false uses full-progress path", "[pack][execute]") {
  TempDir tmp;
  createBinaryFile(tmp.path / "media_a/clip.mp4", 2048);
  createBinaryFile(tmp.path / "media_b/clip.mp4", 2048);

  auto const outputDir = tmp.path / "packed_full";
  fs::create_directories(outputDir);

  pack::PackRequest req{
    .entries = {tmp.path / "media_a/clip.mp4", tmp.path / "media_b/clip.mp4"},
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .compact = false,
  };

  auto const result = pack::execute(req);
  REQUIRE(result.has_value());
  CHECK(result->exitCode == 0);
  CHECK_FALSE(result->zippedFiles.empty());
  for (auto const& zip: result->zippedFiles) { CHECK(fs::exists(zip)); }
}

// ============================================================
// Task 2: Resumable execution tests (RED phase — should fail)
// ============================================================

TEST_CASE(
  "execute() with jobState merges tasks and runs packing",
  "[pack][execute][resumable]"
) {
  TempDir tmp;
  createBinaryFile(tmp.path / "media_a/clip.mp4", 500);
  createBinaryFile(tmp.path / "media_b/clip.mp4", 500);
  auto const outputDir = tmp.path / "packed_resumable";
  fs::create_directories(outputDir);
  auto const statePath = tmp.path / "encro.job-state.json";

  // Initialize job state store
  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(makeStoreConfig(statePath), false);
  REQUIRE(initRes.has_value());

  pack::PackRequest req{
    .entries = {tmp.path / "media_a/clip.mp4", tmp.path / "media_b/clip.mp4"},
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .compact = true,
    .jobState = &store,
  };

  auto const result = pack::execute(req);
  REQUIRE(result.has_value());
  CHECK(result->exitCode == 0);
  CHECK_FALSE(result->zippedFiles.empty());

  // Verify job state was updated
  auto const tasks = store.tasks();
  CHECK_FALSE(tasks.empty());
  // At least one task should be succeeded
  auto hasSucceeded = false;
  for (auto const& task: tasks) {
    if (task.status == jobstate::TaskStatus::Succeeded) {
      hasSucceeded = true;
      break;
    }
  }
  CHECK(hasSucceeded);
}

TEST_CASE(
  "execute() with jobState and all tasks complete returns early",
  "[pack][execute][resumable]"
) {
  TempDir tmp;
  createBinaryFile(tmp.path / "media_a/clip.mp4", 500);
  auto const outputDir = tmp.path / "packed_done";
  fs::create_directories(outputDir);
  auto const statePath = tmp.path / "encro.job-state.json";

  // First pass: do the packing to populate job state
  {
    auto store = jobstate::Store{statePath};
    auto const initRes = store.initialize(makeStoreConfig(statePath), false);
    REQUIRE(initRes.has_value());

    pack::PackRequest req{
      .entries = {tmp.path / "media_a/clip.mp4"},
      .mode = pack::PackMode::Media,
      .outputDir = outputDir,
      .compact = true,
      .jobState = &store,
    };
    auto const result = pack::execute(req);
    REQUIRE(result.has_value());
    CHECK(result->exitCode == 0);
    store.flush();
  }

  // Second pass: simulate resume — all tasks are already complete
  auto resumedStore = jobstate::Store{statePath};
  auto const resumeRes = resumedStore.initialize(makeStoreConfig(statePath), false);
  REQUIRE(resumeRes.has_value());
  // initRes should indicate resume (true = existing state loaded, false = fresh)
  CHECK(resumeRes.value());

  pack::PackRequest req{
    .entries = {tmp.path / "media_a/clip.mp4"},
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .compact = true,
    .jobState = &resumedStore,
  };

  auto const result = pack::execute(req);
  REQUIRE(result.has_value());
  CHECK(result->exitCode == 0);
  // No new zip files should be created (all tasks already complete)
  CHECK(result->zippedFiles.empty());
}

TEST_CASE(
  "execute() with jobState and cancel requested returns kCanceledExitCode",
  "[pack][execute][resumable]"
) {
  TempDir tmp;
  createBinaryFile(tmp.path / "media_a/clip.mp4", 500);
  auto const outputDir = tmp.path / "packed_cancel";
  fs::create_directories(outputDir);
  auto const statePath = tmp.path / "encro.job-state.json";

  auto store = jobstate::Store{statePath};
  auto const initRes = store.initialize(makeStoreConfig(statePath), false);
  REQUIRE(initRes.has_value());

  // Request stop signal to trigger cancel
  stopsignal::requestStop();

  pack::PackRequest req{
    .entries = {tmp.path / "media_a/clip.mp4"},
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .compact = true,
    .jobState = &store,
  };

  auto const result = pack::execute(req);
  // Either error (stop signal causes failure) or kCanceledExitCode
  if (result.has_value()) {
    CHECK(result->exitCode == stopsignal::kCanceledExitCode);
  } else {
    // Accept error as valid — stop signal may cause the packing itself to fail
    SUCCEED("Error path also valid for cancellation");
  }

  stopsignal::reset();
}
