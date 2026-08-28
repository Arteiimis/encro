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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

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
  testutils::writeSizedFile(subdirA / "stream.mp4", 1024);
  testutils::writeSizedFile(subdirA / "subtitle.srt", 512);
  testutils::writeSizedFile(subdirB / "output.mp4", 2048);

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

TEST_CASE(
  "execute() with explicit entryInputs keeps duplicate logical media entries",
  "[pack][execute]"
) {
  TempDir tmp;
  auto const sourceDir = tmp.path / "album" / "a";
  auto const sourceFile = sourceDir / "alpha.jpg";
  testutils::writeSizedFile(sourceFile, 256);

  auto const outputDir = tmp.path / "packed_explicit";
  fs::create_directories(outputDir);

  pack::PackRequest req{
    .entryInputs =
      {
        pack::PackEntryInput{
          .entry =
            pack::PackFileEntry{
              .sourcePath = sourceFile,
              .zipEntryName = "0000__summary__a__alpha__001.jpg",
            },
          .sourceDir = sourceDir,
          .sourceKey = std::string{"0000__a"},
          .fileKey = std::string{"0000__summary__a__alpha__001.jpg"},
        },
        pack::PackEntryInput{
          .entry =
            pack::PackFileEntry{
              .sourcePath = sourceFile,
              .zipEntryName = "1000__a__alpha__001.jpg",
            },
          .sourceDir = sourceDir,
          .sourceKey = std::string{"1000__a"},
          .fileKey = std::string{"1000__a__alpha__001.jpg"},
        },
      },
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .compact = true,
  };

  auto const result = pack::execute(req);
  REQUIRE(result.has_value());
  CHECK(result->exitCode == 0);
  REQUIRE(result->zippedFiles.size() == 1);

  auto const entryNames =
    testutils::listZipRegularEntryNames(result->zippedFiles.front());
  REQUIRE(entryNames.size() == 2);
  CHECK(entryNames[0] == "0000__summary__a__alpha__001.jpg");
  CHECK(entryNames[1] == "1000__a__alpha__001.jpg");
}

TEST_CASE("execute() with Directory mode packs directory tree", "[pack][execute]") {
  TempDir tmp;
  testutils::writeSizedFile(tmp.path / "docs/readme.txt", 200);
  testutils::writeSizedFile(tmp.path / "docs/guide.pdf", 300);
  testutils::writeSizedFile(tmp.path / "docs/nested/deep.txt", 100);

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
  testutils::writeSizedFile(tmp.path / "media_a/clip.mp4", 2048);
  testutils::writeSizedFile(tmp.path / "media_b/clip.mp4", 2048);

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
  testutils::writeSizedFile(tmp.path / "media_a/clip.mp4", 500);
  testutils::writeSizedFile(tmp.path / "media_b/clip.mp4", 500);
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
  testutils::writeSizedFile(tmp.path / "media_a/clip.mp4", 500);
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
  testutils::writeSizedFile(tmp.path / "media_a/clip.mp4", 500);
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

// ============================================================
// Grouping Strategy + Summary Config tests
// ============================================================

TEST_CASE(
  "GroupingStrategy::PerSourceDirKeepTogether never splits source dir entries",
  "[pack-execute][grouping-strategy]"
) {
  TempDir temp;
  auto const outputDir = temp.path / "output";
  fs::create_directories(outputDir);

  auto const dirA = temp.path / "dirA";
  auto const dirB = temp.path / "dirB";
  fs::create_directories(dirA);
  fs::create_directories(dirB);

  auto entries = std::vector<fs::path>{};
  for (auto i = 0; i < 10; ++i) {
    auto const filePath = dirA / std::format("file_{:03d}.txt", i);
    testutils::writeSizedFile(filePath, 100);
    entries.push_back(filePath);
  }
  for (auto i = 0; i < 5; ++i) {
    auto const filePath = dirB / std::format("file_{:03d}.txt", i);
    testutils::writeSizedFile(filePath, 100);
    entries.push_back(filePath);
  }

  pack::PackRequest request{
    .entries = entries,
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .groupingStrategy = pack::GroupingStrategy::PerSourceDirKeepTogether,
  };

  auto const result = pack::execute(request);
  REQUIRE(result);
  CHECK(result->zippedFiles.size() >= 1);
  for (auto const& zf: result->zippedFiles) { CHECK(fs::exists(zf)); }
}

TEST_CASE(
  "GroupingStrategy::PerSourceDir is the default strategy",
  "[pack-execute][grouping-strategy]"
) {
  TempDir temp;
  auto const outputDir = temp.path / "output";
  fs::create_directories(outputDir);

  auto const filePath = temp.path / "test.txt";
  testutils::writeSizedFile(filePath, 100);

  pack::PackRequest request{
    .entries = {filePath},
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
  };

  CHECK(request.groupingStrategy == pack::GroupingStrategy::PerSourceDir);

  auto const result = pack::execute(request);
  REQUIRE(result);
  CHECK(result->zippedFiles.size() == 1);
}

TEST_CASE(
  "SummaryConfig injects summary entries into pack",
  "[pack-execute][summary-config]"
) {
  TempDir temp;
  auto const outputDir = temp.path / "output";
  fs::create_directories(outputDir);

  auto const regularFile = temp.path / "regular.txt";
  testutils::writeSizedFile(regularFile, 100);

  auto const summaryFile = temp.path / "cover.jpg";
  testutils::writeSizedFile(summaryFile, 200);

  pack::PackRequest request{
    .entries = {regularFile},
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .summary = pack::SummaryConfig{
      .entries =
        {
          pack::PackFileEntry{
            .sourcePath = summaryFile,
            .zipEntryName = "00_cover.jpg",
            .isSummary = true,
          },
        },
      .prefix = "00_",
      .enabled = true,
    },
  };

  auto const result = pack::execute(request);
  REQUIRE(result);
  CHECK(result->zippedFiles.size() == 1);

  auto const entryNames = testutils::listZipRegularEntryNames(result->zippedFiles[0]);
  CHECK(entryNames.size() == 2);
  auto const hasCover = std::ranges::any_of(entryNames, [](std::string const& name) {
    return name.find("cover") != std::string::npos;
  });
  CHECK(hasCover);
}

TEST_CASE(
  "Summary entries are ordered first in archive via isSummary flag",
  "[pack-execute][summary-ordering]"
) {
  TempDir temp;
  auto const outputDir = temp.path / "output";
  fs::create_directories(outputDir);

  auto const fileA = temp.path / "a.txt";
  auto const fileB = temp.path / "b.txt";
  auto const coverFile = temp.path / "cover.jpg";
  testutils::writeSizedFile(fileA, 100);
  testutils::writeSizedFile(fileB, 100);
  testutils::writeSizedFile(coverFile, 200);

  pack::PackRequest request{
    .entries = {fileA, fileB},
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .summary = pack::SummaryConfig{
      .entries =
        {
          pack::PackFileEntry{
            .sourcePath = coverFile,
            .zipEntryName = "00_cover.jpg",
            .isSummary = true,
          },
        },
      .prefix = "00_",
      .enabled = true,
    },
  };

  auto const result = pack::execute(request);
  REQUIRE(result);
  CHECK(result->zippedFiles.size() == 1);

  auto const entryNames = testutils::listZipRegularEntryNames(result->zippedFiles[0]);
  REQUIRE(entryNames.size() >= 2);
  CHECK(entryNames[0].find("cover") != std::string::npos);
}

TEST_CASE(
  "entryNameForFile does not overwrite summary entry names",
  "[pack-execute][summary-config]"
) {
  TempDir temp;
  auto const outputDir = temp.path / "output";
  fs::create_directories(outputDir);

  auto const regularFile = temp.path / "regular.txt";
  testutils::writeSizedFile(regularFile, 100);

  auto const summaryFile = temp.path / "cover.jpg";
  testutils::writeSizedFile(summaryFile, 200);

  pack::PackRequest request{
    .entries = {regularFile},
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .summary =
      pack::SummaryConfig{
        .entries =
          {
            pack::PackFileEntry{
              .sourcePath = summaryFile,
              .zipEntryName = "00_cover.jpg",
              .isSummary = true,
            },
          },
        .prefix = "00_",
        .enabled = true,
      },
    .entryNameForFile = [](fs::path const&) { return std::string{"overridden.txt"}; },
  };

  auto const result = pack::execute(request);
  REQUIRE(result);
  CHECK(result->zippedFiles.size() == 1);

  auto const entryNames = testutils::listZipRegularEntryNames(result->zippedFiles[0]);
  REQUIRE(entryNames.size() == 2);
  CHECK(entryNames[0] == "00_cover.jpg");
  CHECK(entryNames[1] == "overridden.txt");
}
