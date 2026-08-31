#include "pack/pack_service.h"
#include "pack/pack_internal.h"
#include "pack/pack_plan_internal.h"
#include "test_utils.h"

#include <libzippp/libzippp.h>

#include <filesystem>
#include <format>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

pack::PackService testService;

}  // namespace

TEST_CASE("packGroups returns empty for empty plan", "[pack-service]") {
  auto const plan = pack::PackPlan{};
  auto const result = testService.packGroups(plan);

  REQUIRE(result);
  CHECK(result.value().empty());
}

TEST_CASE("packGroups packs grouped files", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = testutils::writeTextFile(srcDir / "a.txt");
  auto const f2 = testutils::writeTextFile(srcDir / "b.txt");
  auto const f3 = testutils::writeTextFile(srcDir / "c.txt");

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
    .zipNameForIndex = [](std::size_t index) {
      return std::format("group{}.zip", index + 1);
    },
  };

  auto const result = testService.packGroups(plan);

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

  auto const ranges = pack::internal::buildGroupOrdinalRanges(groups);

  REQUIRE(ranges.size() == 2);
  CHECK(ranges[0].first == 1);
  CHECK(ranges[0].last == 2);
  CHECK(ranges[0].count == 2);
  CHECK(ranges[1].first == 3);
  CHECK(ranges[1].last == 3);
  CHECK(ranges[1].count == 1);
  CHECK(
    pack::internal::appendOrdinalRangeSuffix("bundle_part1.zip", ranges[0])
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
    pack::internal::selectPackPlanIndexes(nonCompactPlan, std::span{selectedIndexes});
  CHECK(resultNonCompact.compact == false);

  // Plan with compact=true (default)
  auto const compactPlan = pack::PackPlan{
    .groups = groups,
    .outputDir = fs::path{},
    .compact = true,
  };

  auto const resultCompact =
    pack::internal::selectPackPlanIndexes(compactPlan, std::span{selectedIndexes});
  CHECK(resultCompact.compact == true);
}

TEST_CASE(
  "selectPackPlanIndexes delegates to named helpers instead of lambda-wrapping-lambda",
  "[pack-service]"
) {
  auto groups = std::vector<std::vector<pack::PackFileEntry>>{
    std::vector<pack::PackFileEntry>{
      pack::PackFileEntry{.sourcePath = fs::path{"a"}, .zipEntryName = "a"},
    },
    std::vector<pack::PackFileEntry>{
      pack::PackFileEntry{.sourcePath = fs::path{"b"}, .zipEntryName = "b"},
    },
  };

  auto const plan = pack::PackPlan{
    .groups = groups,
    .outputDir = fs::path{},
    .zipNameForIndex = [](std::size_t i) { return std::format("arch{}.zip", i); },
  };

  auto const selected = std::vector<std::size_t>{1, 0};
  auto const result = pack::internal::selectPackPlanIndexes(plan, std::span{selected});

  // Verify zipNameForIndex remaps correctly: selected[0]=1 maps to original index 1
  CHECK(result.zipNameForIndex(0) == "arch1.zip");
}

TEST_CASE("packGroups compact mode reports per-file progress updates", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = testutils::writeTextFile(srcDir / "a.txt");
  auto const f2 = testutils::writeTextFile(srcDir / "b.txt");
  auto const f3 = testutils::writeTextFile(srcDir / "c.txt");

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
    .progressCallbacks =
      {
        .onCompactProgress =
          [&](std::size_t completedFiles, std::size_t totalFiles) {
            progressUpdates.push_back(std::format("{}/{}", completedFiles, totalFiles));
          },
      },
    .compact = true,
  };

  auto const result = testService.packGroups(plan);

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

  auto const f1 = testutils::writeTextFile(srcDir / "a.txt");
  auto const f2 = testutils::writeTextFile(srcDir / "b.txt");
  auto const f3 = testutils::writeTextFile(srcDir / "c.txt");

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
    .progressCallbacks =
      {
        .onCompactStatusText =
          [&](std::string_view statusText) { statusTexts.emplace_back(statusText); },
      },
    .compact = true,
  };

  auto const result = testService.packGroups(plan);

  REQUIRE(result);
  // The finalizing spinner emits "Finalizing |"-style animation frames on a
  // 120 ms cadence while the archive closes; under load the archive can take
  // longer than one tick, so tolerate any trailing animation frames after the
  // deterministic packing sequence.
  REQUIRE(statusTexts.size() >= 6);
  CHECK(
    std::vector<std::string>{statusTexts.begin(), statusTexts.begin() + 6}
    == std::vector<std::string>{
      "Packing: archive 0/1 [file 0/3]",
      "Packing: archive 0/1 [file 1/3]",
      "Packing: archive 0/1 [file 2/3]",
      "Packing: archive 0/1 [file 3/3]",
      "Packing: archive 1/1 [file 3/3]",
      "Packed: archive 1/1 complete",
    }
  );
  for (auto it = statusTexts.begin() + 6; it != statusTexts.end(); ++it) {
    CHECK(it->starts_with("Finalizing "));
  }
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

  auto const f1 = testutils::writeTextFile(srcDir / "a.txt");
  auto const f2 = testutils::writeTextFile(srcDir / "b.txt");

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
    .progressCallbacks =
      {
        .onCompactProgress =
          [&](std::size_t completedFiles, std::size_t totalFiles) {
            if (completedFiles == 1) {
              std::this_thread::sleep_for(std::chrono::milliseconds{50});
            }
            progressUpdates.push_back(std::format("{}/{}", completedFiles, totalFiles));
          },
      },
    .maxParallelJobs = 2,
    .compact = true,
  };

  auto const result = testService.packGroups(plan);

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
    .progressCallbacks =
      {
        .onCompactProgress =
          [&](std::size_t completedFiles, std::size_t totalFiles) {
            progressUpdates.push_back(std::format("{}/{}", completedFiles, totalFiles));
          },
      },
    .compact = true,
  };

  auto const result = testService.packGroups(plan);

  REQUIRE(result);
  CHECK(progressUpdates == std::vector<std::string>{"0/1", "1/1"});
}

TEST_CASE("packGroups notifies group success callbacks in both modes", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = testutils::writeTextFile(srcDir / "a.txt");

  auto runCase = [&](bool compact) {
    auto callbackEvents = std::vector<std::string>{};
    auto callbackZipPath = fs::path{};
    auto const plan = pack::PackPlan{
      .groups =
        {
          std::vector<pack::PackFileEntry>{
            pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"},
          },
        },
      .outputDir = outDir,
      .zipNameForIndex = [](std::size_t) { return std::string{"group1.zip"}; },
      .progressCallbacks = {
        .onGroupStart =
          [&](std::size_t index) {
            callbackEvents.push_back(std::format("start:{}", index));
          },
        .onGroupSuccess =
          [&](std::size_t index, fs::path const& zipPath) {
            callbackEvents.push_back(std::format("success:{}", index));
            callbackZipPath = zipPath;
          },
      },
      .compact = compact,
    };

    auto const result = testService.packGroups(plan);

    REQUIRE(result);
    CHECK(callbackEvents == std::vector<std::string>{"start:0", "success:0"});
    CHECK(callbackZipPath == outDir / "group1.zip");
  };

  SECTION("compact") {
    runCase(true);
  }

  SECTION("full") {
    runCase(false);
  }
}

TEST_CASE(
  "packGroups preserves group callback order across multiple groups in both modes",
  "[pack-service]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = testutils::writeTextFile(srcDir / "a.txt");
  auto const f2 = testutils::writeTextFile(srcDir / "b.txt");

  auto runCase = [&](bool compact) {
    auto callbackEvents = std::vector<std::string>{};
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
      .progressCallbacks = {
        .onGroupStart =
          [&](std::size_t index) {
            callbackEvents.push_back(std::format("start:{}", index));
          },
        .onGroupSuccess =
          [&](std::size_t index, fs::path const&) {
            callbackEvents.push_back(std::format("success:{}", index));
          },
      },
      .maxParallelJobs = 1,
      .compact = compact,
    };

    auto const result = testService.packGroups(plan);

    REQUIRE(result);
    CHECK(
      callbackEvents
      == std::vector<std::string>{"start:0", "success:0", "start:1", "success:1"}
    );
  };

  SECTION("compact") {
    runCase(true);
  }

  SECTION("full") {
    runCase(false);
  }
}

TEST_CASE(
  "execute() in Directory mode rejects a non-existent input directory",
  "[pack-service]"
) {
  TempDir temp;
  auto const nonExistentDir = temp.path / "does_not_exist";
  auto const outDir = temp.path / "out";

  pack::PackRequest req{
    .entries = {nonExistentDir},
    .mode = pack::PackMode::Directory,
    .outputDir = outDir,
  };

  auto const result = pack::execute(req);
  REQUIRE_FALSE(result);
  CHECK(result.error().find("not a directory") != std::string::npos);
}

TEST_CASE(
  "execute() in Directory mode disambiguates same-named entries when forced",
  "[pack-service]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const subDir = srcDir / "sub";
  auto const outDir = temp.path / "out";
  fs::create_directories(subDir);

  testutils::writeTextFile(srcDir / "same.txt");
  testutils::writeTextFile(subDir / "same.txt");

  pack::PackRequest req{
    .entries = {srcDir},
    .mode = pack::PackMode::Directory,
    .outputDir = outDir,
    .naming = pack::NamingConfig{
      .namingStrategy = pack::NamingStrategy::FlatWithForce,
    },
  };

  auto const result = pack::execute(req);
  REQUIRE(result);

  auto zipFiles = testutils::listRegularFiles(outDir);
  REQUIRE(zipFiles.size() == 1);

  auto entries = testutils::listZipRegularEntryNames(zipFiles[0]);
  CHECK(entries.size() == 2);
  CHECK(entries[0] != entries[1]);
}

TEST_CASE("packGroups handles non-existent source files gracefully", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  pack::PackPlan plan{
    .groups =
      {
        {
          pack::PackFileEntry{
            .sourcePath = srcDir / "missing.txt",
            .zipEntryName = "missing.txt",
          },
        },
      },
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t) { return std::string{"pack.zip"}; },
  };

  auto const result = testService.packGroups(plan);

  REQUIRE(result);
  CHECK(result.value().size() == 1);
}

TEST_CASE("packGroups reports failure when a group task throws", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);
  auto const f1 = testutils::writeTextFile(srcDir / "a.txt");

  pack::PackPlan plan{
    .groups =
      {
        {pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"}},
      },
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t) { return std::string{"p.zip"}; },
    .progressCallbacks = {
      .onGroupStart = [](std::size_t) {
        throw std::runtime_error{"boom from onGroupStart"};
      },
    },
  };

  auto const result = testService.packGroups(plan);

  // The run must be reported as failed with the exception message, never as a
  // silent success (error-visibility: pack task failures are never success).
  REQUIRE_FALSE(result);
  CHECK(result.error().find("boom from onGroupStart") != std::string::npos);
}
