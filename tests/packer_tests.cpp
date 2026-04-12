#include "pack/packer.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>
#include <libzippp/libzippp.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace indicators;

static auto
createSizedFile(fs::path const& dir, std::string_view name, std::size_t sizeBytes) {
  auto const filePath = dir / name;
  std::ofstream out{filePath, std::ios::binary};
  std::string content(sizeBytes, 'x');
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  return filePath;
}

TEST_CASE(
  "groupFilesBySize splits sequentially by limit",
  "[packer][groupFilesBySize]"
) {
  TempDir temp;
  auto const f1 = createSizedFile(temp.path, "a.bin", 100);
  auto const f2 = createSizedFile(temp.path, "b.bin", 150);
  auto const f3 = createSizedFile(temp.path, "c.bin", 250);
  auto const f4 = createSizedFile(temp.path, "d.bin", 50);

  auto const grouped = groupFilesBySize({f1, f2, f3, f4}, 300);

  REQUIRE(grouped.size() == 2);
  CHECK(grouped[0] == std::vector{f1, f2});
  CHECK(grouped[1] == std::vector{f3, f4});
}

TEST_CASE(
  "packFilesToZip archives files and reports progress",
  "[packer][packFilesToZip]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);
  fs::create_directories(outDir);

  auto const f1 = createSizedFile(srcDir, "a.txt", 64);
  auto const f2 = createSizedFile(srcDir, "b.txt", 128);
  auto const zipPath = outDir / "bundle.zip";

  progress::ProgressContext progressCtx;

  auto const result =
    packFilesToZip({f1, f2}, zipPath, progressCtx, "Packing: bundle.zip");

  REQUIRE(result);
  REQUIRE(fs::exists(zipPath));

  libzippp::ZipArchive zip{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  auto const entries = zip.getEntries();
  REQUIRE(entries.size() == 2);
  auto entryNames = std::vector<std::string>{};
  entryNames.reserve(entries.size());
  for (auto const& entry: entries) { entryNames.emplace_back(entry.getName()); }
  std::ranges::sort(entryNames);

  auto expectedNames =
    std::vector<std::string>{f1.filename().string(), f2.filename().string()};
  std::ranges::sort(expectedNames);

  CHECK(entryNames == expectedNames);
  zip.close();
}

TEST_CASE(
  "packFilesToZip disambiguates duplicate entry names by default",
  "[packer][packFilesToZip]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const dirA = srcDir / "a";
  auto const dirB = srcDir / "b";
  auto const outDir = temp.path / "out";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  fs::create_directories(outDir);

  auto const f1 = createSizedFile(dirA, "same.txt", 64);
  auto const f2 = createSizedFile(dirB, "same.txt", 128);
  auto const zipPath = outDir / "bundle.zip";

  progress::ProgressContext progressCtx;

  auto const result =
    packFilesToZip({f1, f2}, zipPath, progressCtx, "Packing: bundle.zip");

  REQUIRE(result);

  libzippp::ZipArchive zip{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  auto const entries = zip.getEntries();
  REQUIRE(entries.size() == 2);
  auto entryNames = std::vector<std::string>{};
  entryNames.reserve(entries.size());
  for (auto const& entry: entries) { entryNames.emplace_back(entry.getName()); }

  CHECK(std::ranges::count(entryNames, std::string{"same.txt"}) == 1);
  CHECK(
    std::ranges::count_if(entryNames, [](std::string const& name) {
      return name != "same.txt" && name.starts_with("same__")
          && name.ends_with(".txt");
    })
    == 1
  );
  zip.close();
}

TEST_CASE(
  "packFilesToZip preserves relative entry names when resolver is provided",
  "[packer][packFilesToZip]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const dirA = srcDir / "a";
  auto const dirB = srcDir / "b";
  auto const outDir = temp.path / "out";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  fs::create_directories(outDir);

  auto const f1 = createSizedFile(dirA, "same.txt", 64);
  auto const f2 = createSizedFile(dirB, "same.txt", 128);
  auto const zipPath = outDir / "bundle.zip";

  progress::ProgressContext progressCtx;

  auto const result = packFilesToZip(
    {f1, f2},
    zipPath,
    progressCtx,
    "Packing: bundle.zip",
    [srcDir](fs::path const& filePath) {
      return filePath.lexically_relative(srcDir).generic_string();
    }
  );

  REQUIRE(result);

  libzippp::ZipArchive zip{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  auto const entries = zip.getEntries();
  auto entryNames = std::vector<std::string>{};
  entryNames.reserve(entries.size());
  for (auto const& entry: entries) {
    if (entry.getName().ends_with('/')) { continue; }
    entryNames.emplace_back(entry.getName());
  }
  REQUIRE(entryNames.size() == 2);
  std::ranges::sort(entryNames);

  CHECK(entryNames == std::vector<std::string>{"a/same.txt", "b/same.txt"});
  zip.close();
}

TEST_CASE(
  "packAllFilesInDirectory packs all files with size grouping",
  "[packer][packAllFilesInDirectory]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "input";
  auto const outputDir = temp.path / "packed";
  fs::create_directories(inputDir);

  auto const f1 = createSizedFile(inputDir, "a.bin", 150);
  auto const f2 = createSizedFile(inputDir, "b.bin", 150);
  auto const f3 = createSizedFile(inputDir, "c.bin", 60);

  auto const packRes = packAllFilesInDirectory(inputDir, outputDir, 300, true);

  REQUIRE(packRes);
  REQUIRE(fs::exists(outputDir / "input_part1_1-2_items2.zip"));
  REQUIRE(fs::exists(outputDir / "input_part2_3-3_items1.zip"));

  libzippp::ZipArchive zip1{
    (outputDir / "input_part1_1-2_items2.zip").string()
  };
  zip1.open(libzippp::ZipArchive::ReadOnly);
  CHECK(zip1.getEntries().size() == 2);
  zip1.close();

  libzippp::ZipArchive zip2{
    (outputDir / "input_part2_3-3_items1.zip").string()
  };
  zip2.open(libzippp::ZipArchive::ReadOnly);
  CHECK(zip2.getEntries().size() == 1);
  zip2.close();
}

TEST_CASE(
  "packAllFilesInDirectory respects non-recursive option",
  "[packer][packAllFilesInDirectory]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "input";
  auto const nestedDir = inputDir / "nested";
  auto const outputDir = temp.path / "packed";
  fs::create_directories(nestedDir);

  auto const topFile = createSizedFile(inputDir, "top.bin", 64);
  auto const nestedFile = createSizedFile(nestedDir, "nested.bin", 64);

  auto const packRes = packAllFilesInDirectory(inputDir, outputDir, 300, false);

  REQUIRE(packRes);
  libzippp::ZipArchive zip{
    (outputDir / "input_part1_1-1_items1.zip").string()
  };
  zip.open(libzippp::ZipArchive::ReadOnly);
  auto const entries = zip.getEntries();
  REQUIRE(entries.size() == 1);
  CHECK(entries.front().getName() == topFile.filename().string());
  CHECK(entries.front().getName() != nestedFile.filename().string());
  zip.close();
}
