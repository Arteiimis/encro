#include "pack/pack_service.h"
#include "pack/pack_types.h"
#include "pack/pack_plan_internal.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>
#include <libzippp/libzippp.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <vector>

namespace fs = std::filesystem;

namespace {

auto createFile(fs::path const& dir, std::string_view name) -> fs::path {
  auto const filePath = dir / name;
  std::ofstream out{filePath, std::ios::binary};
  out << "test data " << name;
  return filePath;
}

}  // namespace

TEST_CASE("packAllFilesInDirectory packs files from a real directory", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  createFile(srcDir, "a.txt");
  createFile(srcDir, "b.txt");
  createFile(srcDir, "c.txt");

  pack::PackService service;

  auto result = service.packAllFilesInDirectory(
    srcDir,
    outDir,
    500ULL * 1024 * 1024,
    true,
    pack::NamingStrategy::Flat,
    std::nullopt
  );

  REQUIRE(result);

  auto zipFiles = testutils::listRegularFiles(outDir);
  REQUIRE(zipFiles.size() == 1);
  CHECK(zipFiles[0].extension() == ".zip");

  auto entries = testutils::listZipRegularEntryNames(zipFiles[0]);
  CHECK(entries == std::vector<std::string>{"a.txt", "b.txt", "c.txt"});
}

TEST_CASE(
  "packAllFilesInDirectory returns error for non-existent directory",
  "[pack-service]"
) {
  TempDir temp;
  auto const nonExistentDir = temp.path / "does_not_exist";
  auto const outDir = temp.path / "out";

  pack::PackService service;

  auto result = service.packAllFilesInDirectory(
    nonExistentDir,
    outDir,
    500ULL * 1024 * 1024,
    true,
    pack::NamingStrategy::Flat,
    std::nullopt
  );

  REQUIRE_FALSE(result);
  CHECK(result.error().find("not a directory") != std::string::npos);
}

TEST_CASE("packAllFilesInDirectory respects non-recursive flag", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const subDir = srcDir / "sub";
  auto const outDir = temp.path / "out";
  fs::create_directories(subDir);

  createFile(srcDir, "root.txt");
  createFile(subDir, "nested.txt");

  pack::PackService service;

  auto result = service.packAllFilesInDirectory(
    srcDir,
    outDir,
    500ULL * 1024 * 1024,
    false,
    pack::NamingStrategy::Flat,
    std::nullopt
  );

  REQUIRE(result);

  auto zipFiles = testutils::listRegularFiles(outDir);
  REQUIRE(zipFiles.size() == 1);

  auto entries = testutils::listZipRegularEntryNames(zipFiles[0]);
  CHECK(entries == std::vector<std::string>{"root.txt"});
}

TEST_CASE("packAllFilesInDirectory with forceNameConflictHandling", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const subDir = srcDir / "sub";
  auto const outDir = temp.path / "out";
  fs::create_directories(subDir);

  createFile(srcDir, "same.txt");
  createFile(subDir, "same.txt");

  pack::PackService service;

  auto result = service.packAllFilesInDirectory(
    srcDir,
    outDir,
    500ULL * 1024 * 1024,
    true,
    pack::NamingStrategy::FlatWithForce,
    std::nullopt
  );

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

  pack::PackService service;

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

  auto result = service.packGroups(plan);

  REQUIRE(result);
  CHECK(result.value().size() == 1);
}

TEST_CASE("packGroups creates zip at correct output path", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = createFile(srcDir, "a.txt");

  pack::PackService service;

  pack::PackPlan plan{
    .groups =
      {
        {pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"}},
      },
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t) { return std::string{"custom_name.zip"}; },
  };

  auto result = service.packGroups(plan);

  REQUIRE(result);
  REQUIRE(result.value().size() == 1);
  CHECK(result.value()[0] == outDir / "custom_name.zip");
  CHECK(fs::exists(outDir / "custom_name.zip"));

  auto entries = testutils::listZipRegularEntryNames(outDir / "custom_name.zip");
  CHECK(entries == std::vector<std::string>{"a.txt"});
}

TEST_CASE("packGroups returns correct zipped file paths", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = createFile(srcDir, "a.txt");
  auto const f2 = createFile(srcDir, "b.txt");

  pack::PackService service;

  pack::PackPlan plan{
    .groups =
      {
        {pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"}},
        {pack::PackFileEntry{.sourcePath = f2, .zipEntryName = "b.txt"}},
      },
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t i) { return std::format("p{}.zip", i + 1); },
  };

  auto result = service.packGroups(plan);

  REQUIRE(result);
  REQUIRE(result.value().size() == 2);
  CHECK(result.value()[0] == outDir / "p1.zip");
  CHECK(result.value()[1] == outDir / "p2.zip");
  CHECK(fs::exists(outDir / "p1.zip"));
  CHECK(fs::exists(outDir / "p2.zip"));
}

TEST_CASE("packGroups compact mode writes correct zip content", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = createFile(srcDir, "a.txt");
  auto const f2 = createFile(srcDir, "b.txt");

  pack::PackService service;

  pack::PackPlan plan{
    .groups =
      {
        {
          pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"},
          pack::PackFileEntry{.sourcePath = f2, .zipEntryName = "b.txt"},
        },
      },
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t) { return std::string{"cmpt.zip"}; },
    .compact = true,
  };

  auto result = service.packGroups(plan);

  REQUIRE(result);
  REQUIRE(fs::exists(outDir / "cmpt.zip"));

  auto entries = testutils::listZipRegularEntryNames(outDir / "cmpt.zip");
  CHECK(entries == std::vector<std::string>{"a.txt", "b.txt"});
}

TEST_CASE("packGroups full-progress mode writes correct zip content", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = createFile(srcDir, "a.txt");

  pack::PackService service;

  pack::PackPlan plan{
    .groups =
      {
        {pack::PackFileEntry{.sourcePath = f1, .zipEntryName = "a.txt"}},
      },
    .outputDir = outDir,
    .zipNameForIndex = [](std::size_t) { return std::string{"full.zip"}; },
    .compact = false,
  };

  auto result = service.packGroups(plan);

  REQUIRE(result);
  REQUIRE(fs::exists(outDir / "full.zip"));

  auto entries = testutils::listZipRegularEntryNames(outDir / "full.zip");
  CHECK(entries == std::vector<std::string>{"a.txt"});
}
