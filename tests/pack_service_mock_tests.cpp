#include "pack/pack_service.h"
#include "pack/pack_types.h"
#include "packer_mock.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <memory>
#include <vector>

namespace fs = std::filesystem;

using namespace pack::test;

TEST_CASE(
  "packGroups delegates to IPacker::packFilesToZip for each group",
  "[pack-service][mock]"
) {
  auto mock = std::make_unique<MockPacker>();
  auto& mockRef = *mock;
  pack::PackService service(std::move(mock));

  pack::PackPlan plan{
    .groups =
      {
        {pack::PackFileEntry{.sourcePath = "a.txt", .zipEntryName = "a.txt"}},
        {pack::PackFileEntry{.sourcePath = "b.txt", .zipEntryName = "b.txt"}},
      },
    .outputDir = "/out",
    .zipNameForIndex = [](std::size_t i) { return std::format("part{}.zip", i + 1); },
  };

  auto result = service.packGroups(plan);

  REQUIRE(result);
  REQUIRE(mockRef.packFilesToZipCalls.size() == 2);
  CHECK(mockRef.packFilesToZipCalls[0].entries.size() == 1);
  CHECK(mockRef.packFilesToZipCalls[1].entries.size() == 1);
}

TEST_CASE("packGroups passes correct zip paths", "[pack-service][mock]") {
  auto mock = std::make_unique<MockPacker>();
  auto& mockRef = *mock;
  pack::PackService service(std::move(mock));

  pack::PackPlan plan{
    .groups =
      {
        {pack::PackFileEntry{.sourcePath = "a.txt", .zipEntryName = "a.txt"}},
      },
    .outputDir = "/out",
    .zipNameForIndex = [](std::size_t) { return std::string{"custom.zip"}; },
  };

  auto result = service.packGroups(plan);

  REQUIRE(result);
  REQUIRE(mockRef.packFilesToZipCalls.size() == 1);
  CHECK(mockRef.packFilesToZipCalls[0].zipFilePath == "/out/custom.zip");
}

TEST_CASE("packGroups propagates IPacker errors", "[pack-service][mock]") {
  auto mock = std::make_unique<MockPacker>();
  auto& mockRef = *mock;
  mockRef.packFilesToZipResult = eh::makeError("simulated pack failure");
  pack::PackService service(std::move(mock));

  pack::PackPlan plan{
    .groups =
      {
        {pack::PackFileEntry{.sourcePath = "a.txt", .zipEntryName = "a.txt"}},
      },
    .outputDir = "/out",
  };

  auto result = service.packGroups(plan);

  REQUIRE_FALSE(result);
  CHECK(result.error().find("simulated pack failure") != std::string::npos);
}

TEST_CASE("packGroups returns zipped file paths", "[pack-service][mock]") {
  auto mock = std::make_unique<MockPacker>();
  auto& mockRef = *mock;
  pack::PackService service(std::move(mock));

  pack::PackPlan plan{
    .groups =
      {
        {pack::PackFileEntry{.sourcePath = "a.txt", .zipEntryName = "a.txt"}},
        {pack::PackFileEntry{.sourcePath = "b.txt", .zipEntryName = "b.txt"}},
      },
    .outputDir = "/out",
    .zipNameForIndex = [](std::size_t i) { return std::format("p{}.zip", i + 1); },
  };

  auto result = service.packGroups(plan);

  REQUIRE(result);
  REQUIRE(result.value().size() == 2);
  CHECK(result.value()[0] == "/out/p1.zip");
  CHECK(result.value()[1] == "/out/p2.zip");
}

TEST_CASE("packGroups empty plan returns empty", "[pack-service][mock]") {
  auto mock = std::make_unique<MockPacker>();
  auto& mockRef = *mock;
  pack::PackService service(std::move(mock));

  pack::PackPlan plan{};

  auto result = service.packGroups(plan);

  REQUIRE(result);
  CHECK(result.value().empty());
  CHECK(mockRef.packFilesToZipCalls.empty());
}

TEST_CASE("packGroups compact mode calls compact overload", "[pack-service][mock]") {
  auto mock = std::make_unique<MockPacker>();
  auto& mockRef = *mock;
  pack::PackService service(std::move(mock));

  pack::PackPlan plan{
    .groups =
      {
        {pack::PackFileEntry{.sourcePath = "a.txt", .zipEntryName = "a.txt"}},
      },
    .outputDir = "/out",
    .compact = true,
  };

  auto result = service.packGroups(plan);

  REQUIRE(result);
  REQUIRE(mockRef.packFilesToZipCalls.size() == 1);
  CHECK(mockRef.packFilesToZipCalls[0].isCompact == true);
}

TEST_CASE("packGroups full-progress mode calls full overload", "[pack-service][mock]") {
  auto mock = std::make_unique<MockPacker>();
  auto& mockRef = *mock;
  pack::PackService service(std::move(mock));

  pack::PackPlan plan{
    .groups =
      {
        {pack::PackFileEntry{.sourcePath = "a.txt", .zipEntryName = "a.txt"}},
      },
    .outputDir = "/out",
    .compact = false,
  };

  auto result = service.packGroups(plan);

  REQUIRE(result);
  REQUIRE(mockRef.packFilesToZipCalls.size() == 1);
  CHECK(mockRef.packFilesToZipCalls[0].isCompact == false);
}

TEST_CASE(
  "packGroups compact mode passes finalizingCount pointer",
  "[pack-service][mock]"
) {
  auto mock = std::make_unique<MockPacker>();
  auto& mockRef = *mock;
  pack::PackService service(std::move(mock));

  pack::PackPlan plan{
    .groups =
      {
        {pack::PackFileEntry{.sourcePath = "a.txt", .zipEntryName = "a.txt"}},
      },
    .outputDir = "/out",
    .compact = true,
  };

  auto result = service.packGroups(plan);

  REQUIRE(result);
  REQUIRE(mockRef.packFilesToZipCalls.size() == 1);
  CHECK(mockRef.packFilesToZipCalls[0].isCompact == true);
  CHECK(mockRef.packFilesToZipCalls[0].finalizingCount != nullptr);
}

TEST_CASE(
  "packAllFilesInDirectory delegates to buildDirectoryPackPlan",
  "[pack-service][mock]"
) {
  auto mock = std::make_unique<MockPacker>();
  auto& mockRef = *mock;
  pack::PackService service(std::move(mock));

  auto result = service.packAllFilesInDirectory(
    fs::path{"/input"},
    fs::path{"/output"},
    500ULL * 1024 * 1024,
    true,
    false,
    std::nullopt
  );

  REQUIRE(result);
  REQUIRE(mockRef.buildPlanCalls.size() == 1);
  CHECK(mockRef.buildPlanCalls[0].dirPath == "/input");
  CHECK(mockRef.buildPlanCalls[0].zipFileDir == "/output");
  CHECK(mockRef.buildPlanCalls[0].maxGroupSize == 500ULL * 1024 * 1024);
  CHECK(mockRef.buildPlanCalls[0].recursive == true);
  CHECK(mockRef.buildPlanCalls[0].forceNameConflictHandling == false);
}

TEST_CASE("packAllFilesInDirectory propagates buildPlan error", "[pack-service][mock]") {
  auto mock = std::make_unique<MockPacker>();
  auto& mockRef = *mock;
  mockRef.buildPlanResult = eh::makeError("no files found");
  pack::PackService service(std::move(mock));

  auto result = service.packAllFilesInDirectory(
    fs::path{"/input"},
    fs::path{"/output"},
    500ULL * 1024 * 1024,
    false,
    false,
    std::nullopt
  );

  REQUIRE_FALSE(result);
  CHECK(result.error().find("no files found") != std::string::npos);
  CHECK(mockRef.packFilesToZipCalls.empty());
}
