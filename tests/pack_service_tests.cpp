#include "pack/pack_service.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>
#include <libzippp/libzippp.h>

#include <filesystem>
#include <format>
#include <fstream>
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

TEST_CASE("packGroupsParallel returns empty for empty plan", "[pack-service]") {
  auto const plan = pack::PackPlan{};
  auto const result = pack::packGroupsParallel(plan);

  REQUIRE(result);
  CHECK(result.value().empty());
}

TEST_CASE("packGroupsParallel packs grouped files", "[pack-service]") {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);

  auto const f1 = createFile(srcDir, "a.txt");
  auto const f2 = createFile(srcDir, "b.txt");
  auto const f3 = createFile(srcDir, "c.txt");

  auto const groups = std::vector{
    std::vector<fs::path>{f1, f2},
    std::vector<fs::path>{f3}
  };

  auto const plan = pack::PackPlan{
    .groups = groups,
    .outputDir = outDir,
    .zipNameForIndex =
      [](std::size_t index) { return std::format("group{}.zip", index + 1); },
    .progressLabelForIndex =
      [](std::size_t index) {
        return std::format("Packing: group{}.zip", index + 1);
      }
  };

  auto const result = pack::packGroupsParallel(plan);

  REQUIRE(result);
  REQUIRE(result.value().size() == 2);
  CHECK(fs::exists(outDir / "group1.zip"));
  CHECK(fs::exists(outDir / "group2.zip"));

  libzippp::ZipArchive zip{(outDir / "group1.zip").string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  CHECK(zip.getEntries().size() == 2);
  zip.close();
}
