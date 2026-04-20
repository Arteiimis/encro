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

auto sourcePathsOf(std::vector<pack::PackFileEntry> const& entries)
  -> std::vector<fs::path> {
  auto paths = std::vector<fs::path>{};
  paths.reserve(entries.size());
  for (auto const& entry: entries) { paths.push_back(entry.sourcePath); }
  return paths;
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
  config.pictureFolderSummary = true;
  config.inputPath = inputDir;

  auto const scannedPics = std::vector<fs::path>{f1, f2, f3};
  auto const planRes = buildPicturePackPlan(config, inputDir, outputDir, scannedPics);

  REQUIRE(planRes);
  REQUIRE(planRes->groups.size() == 2);
  CHECK(sourcePathsOf(planRes->groups[0]) == std::vector<fs::path>{f1, f2});
  CHECK(sourcePathsOf(planRes->groups[1]) == std::vector<fs::path>{f3});
  CHECK(planRes->zipNameForIndex(0) == "pics_part1.1[1~2#2p].zip");
  CHECK(planRes->zipNameForIndex(1) == "pics_part1.2[3~3#1p].zip");
}

TEST_CASE(
  "buildPicturePackPlan injects flat summary entries before normal entries",
  "[picture-process]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const outputDir = temp.path / "packed";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);

  auto const a1 = createSparseSizedFile(dirA, "alpha.jpg", 32);
  auto const a2 = createSparseSizedFile(dirA, "beta.jpg", 32);
  auto const b1 = createSparseSizedFile(dirB, "alpha.jpg", 32);
  auto const b2 = createSparseSizedFile(dirB, "beta.jpg", 32);

  auto config = appctx::AppConfig{};
  config.processType = "picture";
  config.recursive = true;
  config.yesToAll = true;
  config.pictureFolderSummary = true;
  config.inputPath = inputDir;

  auto const scannedPics = std::vector<fs::path>{a1, a2, b1, b2};
  auto const planRes = buildPicturePackPlan(config, inputDir, outputDir, scannedPics);

  REQUIRE(planRes);
  REQUIRE(planRes->groups.size() == 1);
  REQUIRE(planRes->groups[0].size() == 6);

  auto entryNames = std::vector<std::string>{};
  entryNames.reserve(planRes->groups[0].size());
  for (auto const& entry: planRes->groups[0]) {
    entryNames.push_back(entry.zipEntryName);
    CHECK(entry.zipEntryName.find('/') == std::string::npos);
  }
  std::ranges::sort(entryNames);

  CHECK(entryNames[0].starts_with("0000__summary__"));
  CHECK(entryNames[1].starts_with("0000__summary__"));
  CHECK(entryNames[2].starts_with("1000__"));
  CHECK(entryNames[3].starts_with("1000__"));
  CHECK(entryNames[4].starts_with("1000__"));
  CHECK(entryNames[5].starts_with("1000__"));
}
