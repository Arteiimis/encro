#include "core/job_state.h"
#include "pack/pack_service.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>
#include <libzippp/libzippp.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

auto createFile(fs::path const& dir, std::string_view name) -> fs::path {
  auto const filePath = dir / name;
  std::ofstream out{filePath, std::ios::binary};
  out << "data";
  return filePath;
}

}  // namespace

TEST_CASE("packGroups returns empty for empty plan", "[pack-service]") {
  auto const plan = pack::PackPlan{};
  auto const result = pack::packGroups(plan);

  REQUIRE(result);
  CHECK(result.value().empty());
}

TEST_CASE("packGroups packs grouped files", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = createFile(srcDir, "a.txt");
  auto const f2 = createFile(srcDir, "b.txt");
  auto const f3 = createFile(srcDir, "c.txt");

  auto const groups = std::vector{
    std::vector<pack::PackFileEntry>{
      pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"},
      pack::PackFileEntry{.sourcePath = f2, .zipEntryName = "b.txt"},
    },
    std::vector<pack::PackFileEntry>{
      pack::PackFileEntry{.sourcePath = f3, .zipEntryName = "c.txt"},
    }
  };

  auto const plan = pack::PackPlan{
    .groups = groups,
    .outputDir = outDir,
    .zipNameForIndex =
      [](std::size_t index) { return std::format("group{}.zip", index + 1); },
    .progressLabelForIndex =
      [](std::size_t index) { return std::format("Packing: group{}.zip", index + 1); }
  };

  auto const result = pack::packGroups(plan);

  REQUIRE(result);
  REQUIRE(result.value().size() == 2);
  CHECK(fs::exists(outDir / "group1.zip"));
  CHECK(fs::exists(outDir / "group2.zip"));

  libzippp::ZipArchive zip{(outDir / "group1.zip").string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  CHECK(zip.getEntries().size() == 2);
  zip.close();
}

TEST_CASE("pack range helpers append cumulative ordinal suffixes", "[pack-service]") {
  auto const groups = std::vector{
    std::vector<fs::path>{fs::path{"a"}, fs::path{"b"}},
    std::vector<fs::path>{fs::path{"c"}}
  };

  auto const ranges = pack::buildGroupOrdinalRanges(groups);

  REQUIRE(ranges.size() == 2);
  CHECK(ranges[0].first == 1);
  CHECK(ranges[0].last == 2);
  CHECK(ranges[0].count == 2);
  CHECK(ranges[1].first == 3);
  CHECK(ranges[1].last == 3);
  CHECK(ranges[1].count == 1);
  CHECK(
    pack::appendOrdinalRangeSuffix("bundle_part1.zip", ranges[0])
    == "bundle_part1[1~2#2p].zip"
  );
}

TEST_CASE("selectPackPlanIndexes preserves compact from source plan", "[pack-service]") {
  auto groups = std::vector<std::vector<pack::PackFileEntry>>{
    std::vector<pack::PackFileEntry>{
      pack::PackFileEntry{.sourcePath = fs::path{"a"}, .zipEntryName = "a"},
    },
    std::vector<pack::PackFileEntry>{
      pack::PackFileEntry{.sourcePath = fs::path{"b"}, .zipEntryName = "b"},
    },
  };

  // Plan with compact=false (full-progress mode)
  auto const nonCompactPlan = pack::PackPlan{
    .groups = groups,
    .outputDir = fs::path{},
    .compact = false,
  };
  auto const selectedIndexes = std::vector<std::size_t>{0, 1};

  auto const resultNonCompact =
    pack::selectPackPlanIndexes(nonCompactPlan, std::span{selectedIndexes});
  CHECK(resultNonCompact.compact == false);

  // Plan with compact=true (default)
  auto const compactPlan = pack::PackPlan{
    .groups = groups,
    .outputDir = fs::path{},
    .compact = true,
  };

  auto const resultCompact =
    pack::selectPackPlanIndexes(compactPlan, std::span{selectedIndexes});
  CHECK(resultCompact.compact == true);
}

TEST_CASE("packGroups compact mode reports per-file progress updates", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = createFile(srcDir, "a.txt");
  auto const f2 = createFile(srcDir, "b.txt");
  auto const f3 = createFile(srcDir, "c.txt");

  auto progressUpdates = std::vector<std::string>{};
  auto const plan = pack::PackPlan{
    .groups =
      {
        std::vector<pack::PackFileEntry>{
          pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"},
          pack::PackFileEntry{.sourcePath = f2, .zipEntryName = "b.txt"},
          pack::PackFileEntry{.sourcePath = f3, .zipEntryName = "c.txt"},
        },
      },
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t) { return std::string{"group1.zip"}; },
    .onCompactProgress =
      [&](std::size_t completedFiles, std::size_t totalFiles) {
        progressUpdates.push_back(std::format("{}/{}", completedFiles, totalFiles));
      },
    .compact = true,
  };

  auto const result = pack::packGroups(plan);

  REQUIRE(result);
  CHECK(progressUpdates == std::vector<std::string>{"0/3", "1/3", "2/3", "3/3"});
}

TEST_CASE(
  "packGroups compact mode emits archive and file status text",
  "[pack-service]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = createFile(srcDir, "a.txt");
  auto const f2 = createFile(srcDir, "b.txt");
  auto const f3 = createFile(srcDir, "c.txt");

  auto statusTexts = std::vector<std::string>{};
  auto const plan = pack::PackPlan{
    .groups =
      {
        std::vector<pack::PackFileEntry>{
          pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"},
          pack::PackFileEntry{.sourcePath = f2, .zipEntryName = "b.txt"},
          pack::PackFileEntry{.sourcePath = f3, .zipEntryName = "c.txt"},
        },
      },
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t) { return std::string{"group1.zip"}; },
    .onCompactStatusText =
      [&](std::string_view statusText) { statusTexts.emplace_back(statusText); },
    .compact = true,
  };

  auto const result = pack::packGroups(plan);

  REQUIRE(result);
  CHECK(
    statusTexts
    == std::vector<std::string>{
      "Packing: archive 1/1 [file 0/3]",
      "Packing: archive 1/1 [file 1/3]",
      "Packing: archive 1/1 [file 2/3]",
      "Packing: archive 1/1 [file 3/3]",
      "Packed: archive 1/1 complete",
    }
  );
}

TEST_CASE(
  "packGroups compact mode keeps per-file progress callbacks ordered across parallel "
  "groups",
  "[pack-service]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = createFile(srcDir, "a.txt");
  auto const f2 = createFile(srcDir, "b.txt");

  auto progressUpdates = std::vector<std::string>{};
  auto const plan = pack::PackPlan{
    .groups =
      {
        std::vector<pack::PackFileEntry>{
          pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"},
        },
        std::vector<pack::PackFileEntry>{
          pack::PackFileEntry{.sourcePath = f2, .zipEntryName = "b.txt"},
        },
      },
    .outputDir = outDir,
    .zipNameForIndex =
      [](std::size_t index) { return std::format("group{}.zip", index + 1); },
    .onCompactProgress =
      [&](std::size_t completedFiles, std::size_t totalFiles) {
        if (completedFiles == 1) {
          std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        progressUpdates.push_back(std::format("{}/{}", completedFiles, totalFiles));
      },
    .maxParallelJobs = 2,
    .compact = true,
  };

  auto const result = pack::packGroups(plan);

  REQUIRE(result);
  CHECK(progressUpdates == std::vector<std::string>{"0/2", "1/2", "2/2"});
}

TEST_CASE(
  "packGroups compact mode advances progress for skipped entries",
  "[pack-service]"
) {
  TempDir temp;
  auto const outDir = temp.path / "out";

  auto progressUpdates = std::vector<std::string>{};
  auto const missingFile = temp.path / "missing.txt";
  auto const plan = pack::PackPlan{
    .groups =
      {
        std::vector<pack::PackFileEntry>{
          pack::PackFileEntry{.sourcePath = missingFile, .zipEntryName = "missing.txt"},
        },
      },
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t) { return std::string{"group1.zip"}; },
    .onCompactProgress =
      [&](std::size_t completedFiles, std::size_t totalFiles) {
        progressUpdates.push_back(std::format("{}/{}", completedFiles, totalFiles));
      },
    .compact = true,
  };

  auto const result = pack::packGroups(plan);

  REQUIRE(result);
  CHECK(progressUpdates == std::vector<std::string>{"0/1", "1/1"});
}

TEST_CASE(
  "runPackPlan skips already completed archive tasks from job state",
  "[pack-service]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  auto const statePath = temp.path / "state.json";
  fs::create_directories(srcDir);

  auto const f1 = createFile(srcDir, "a.txt");
  auto const f2 = createFile(srcDir, "b.txt");
  auto const zipPath = outDir / "group1.zip";

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "video";
  ctx.config.inputPath = srcDir;
  ctx.config.stateFilePath = statePath;
  ctx.runtime.jobState = std::make_shared<jobstate::Store>(statePath);

  auto const initRes = ctx.runtime.jobState->initialize(ctx.config, false);
  REQUIRE(initRes);

  auto const plan = pack::PackPlan{
    .groups =
      {
        std::vector<pack::PackFileEntry>{
          pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"},
          pack::PackFileEntry{.sourcePath = f2, .zipEntryName = "b.txt"},
        },
      },
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t) { return std::string{"group1.zip"}; },
    .progressLabelForIndex =
      [](std::size_t) { return std::string{"Packing: group1.zip"}; }
  };

  auto const firstRun = pack::runPackPlan(ctx, plan);

  REQUIRE(firstRun);
  REQUIRE(firstRun->exitCode == 0);
  CHECK(firstRun->zippedFiles == std::vector<fs::path>{zipPath});
  CHECK(fs::exists(zipPath));

  auto const secondRun = pack::runPackPlan(ctx, plan);

  REQUIRE(secondRun);
  CHECK(secondRun->exitCode == 0);
  CHECK(secondRun->zippedFiles.empty());

  auto const tasks = ctx.runtime.jobState->tasks();
  REQUIRE(tasks.size() == 1);
  CHECK(tasks.front().status == jobstate::TaskStatus::Succeeded);
}
