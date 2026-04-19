#include "picture/picture_process.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

auto createSparseSizedFile(
  fs::path const& dir,
  std::string_view name,
  std::uintmax_t sizeBytes
) -> fs::path {
  auto const filePath = dir / name;
  auto out = std::ofstream{filePath, std::ios::binary};
  REQUIRE(out.is_open());

  if (sizeBytes > 0) {
    out.seekp(static_cast<std::streamoff>(sizeBytes - 1));
    out.put('\0');
  }

  out.close();
  return filePath;
}

}  // namespace

TEST_CASE(
  "buildPicturePackPlan names size overflow as subpart within the same part",
  "[picture-process]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const outputDir = temp.path / "packed";
  fs::create_directories(inputDir);

  constexpr auto kPictureSize = std::uintmax_t{240ULL * 1024ULL * 1024ULL};
  auto const f1 = createSparseSizedFile(inputDir, "a.jpg", kPictureSize);
  auto const f2 = createSparseSizedFile(inputDir, "b.jpg", kPictureSize);
  auto const f3 = createSparseSizedFile(inputDir, "c.jpg", kPictureSize);

  auto config = appctx::AppConfig{};
  config.processType = "picture";
  config.recursive = true;
  config.yesToAll = true;
  config.inputPath = inputDir;

  auto const scannedPics = std::vector<fs::path>{f1, f2, f3};
  auto const planRes = buildPicturePackPlan(config, inputDir, outputDir, scannedPics);

  REQUIRE(planRes);
  REQUIRE(planRes->groups.size() == 2);
  CHECK(planRes->groups[0] == std::vector<fs::path>{f1, f2});
  CHECK(planRes->groups[1] == std::vector<fs::path>{f3});
  CHECK(planRes->zipNameForIndex(0) == "pics_part1.1[1~2#2p].zip");
  CHECK(planRes->zipNameForIndex(1) == "pics_part1.2[3~3#1p].zip");
}
