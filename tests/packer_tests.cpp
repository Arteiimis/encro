#include "pack/packer.h"
#include "pack/pack_service.h"
#include "test_utils.h"

#include <libzippp/libzippp.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace indicators;

using namespace pack::detail;

static auto
createSizedFile(fs::path const& dir, std::string_view name, std::size_t sizeBytes) {
  auto const filePath = dir / name;
  std::ofstream out{filePath, std::ios::binary};
  std::string content(sizeBytes, 'x');
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  return filePath;
}

TEST_CASE("groupFilesBySize splits sequentially by limit", "[packer][groupFilesBySize]") {
  TempDir temp;
  auto const f1 = createSizedFile(temp.path, "a.bin", 100);
  auto const f2 = createSizedFile(temp.path, "b.bin", 150);
  auto const f3 = createSizedFile(temp.path, "c.bin", 250);
  auto const f4 = createSizedFile(temp.path, "d.bin", 50);

  auto const grouped = pack::Packer{}.groupFilesBySize({f1, f2, f3, f4}, 300);

  REQUIRE(grouped.size() == 2);
  CHECK(grouped[0] == std::vector{f1, f2});
  CHECK(grouped[1] == std::vector{f3, f4});
}

TEST_CASE(
  "groupPackFiles keeps source directories intact after threshold is exceeded",
  "[packer][groupPackFiles]"
) {
  TempDir temp;
  auto const dirA = temp.path / "a";
  auto const dirB = temp.path / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);

  auto const a1 = createSizedFile(dirA, "a1.bin", 100);
  auto const a2 = createSizedFile(dirA, "a2.bin", 100);
  auto const b1 = createSizedFile(dirB, "b1.bin", 90);
  auto const b2 = createSizedFile(dirB, "b2.bin", 90);

  auto const grouped = pack::Packer{}.groupPackFiles(
    {
      PackGroupInput{a1, dirA},
      PackGroupInput{a2, dirA},
      PackGroupInput{b1, dirB},
      PackGroupInput{b2, dirB},
    },
    300,
    std::nullopt,
    3
  );

  REQUIRE(grouped.size() == 2);
  CHECK(grouped[0] == std::vector{a1, a2});
  CHECK(grouped[1] == std::vector{b1, b2});
}

TEST_CASE(
  "groupPackFiles stays sequential before folder carry-over threshold",
  "[packer][groupPackFiles]"
) {
  TempDir temp;
  auto const dirA = temp.path / "a";
  auto const dirB = temp.path / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);

  auto const a1 = createSizedFile(dirA, "a1.bin", 100);
  auto const a2 = createSizedFile(dirA, "a2.bin", 100);
  auto const b1 = createSizedFile(dirB, "b1.bin", 90);
  auto const b2 = createSizedFile(dirB, "b2.bin", 90);

  auto const grouped = pack::Packer{}.groupPackFiles(
    {
      PackGroupInput{a1, dirA},
      PackGroupInput{a2, dirA},
      PackGroupInput{b1, dirB},
      PackGroupInput{b2, dirB},
    },
    300,
    std::nullopt,
    10
  );

  REQUIRE(grouped.size() == 2);
  CHECK(grouped[0] == std::vector{a1, a2, b1});
  CHECK(grouped[1] == std::vector{b2});
}

TEST_CASE(
  "groupPreparedEntries delegates packSourceEntries to named function",
  "[packer][groupPackFiles]"
) {
  TempDir temp;
  auto const dirA = temp.path / "a";
  auto const dirB = temp.path / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);

  auto const a1 = createSizedFile(dirA, "a1.bin", 100);
  auto const a2 = createSizedFile(dirA, "a2.bin", 100);
  auto const b1 = createSizedFile(dirB, "b1.bin", 90);

  auto const grouped = pack::Packer{}.groupPackFiles(
    {
      PackGroupInput{a1, dirA},
      PackGroupInput{a2, dirA},
      PackGroupInput{b1, dirB},
    },
    250,
    std::nullopt,
    2
  );

  REQUIRE(grouped.size() == 2);
  CHECK(grouped[0] == std::vector{a1, a2});
  CHECK(grouped[1] == std::vector{b1});
}

TEST_CASE(
  "groupFilesBySize respects maximum file count per group",
  "[packer][groupFilesBySize]"
) {
  TempDir temp;

  auto const f1 = createSizedFile(temp.path, "a.bin", 8);
  auto const f2 = createSizedFile(temp.path, "b.bin", 8);
  auto const f3 = createSizedFile(temp.path, "c.bin", 8);
  auto const f4 = createSizedFile(temp.path, "d.bin", 8);
  auto const f5 = createSizedFile(temp.path, "e.bin", 8);

  auto const grouped = pack::Packer{}.groupFilesBySize({f1, f2, f3, f4, f5}, 300, 2);

  REQUIRE(grouped.size() == 3);
  CHECK(grouped[0] == std::vector{f1, f2});
  CHECK(grouped[1] == std::vector{f3, f4});
  CHECK(grouped[2] == std::vector{f5});
}

TEST_CASE(
  "groupPackFilesWithSubparts keeps size overflow in the same logical part",
  "[packer][groupPackFilesWithSubparts]"
) {
  TempDir temp;

  auto const f1 = createSizedFile(temp.path, "a.bin", 2);
  auto const f2 = createSizedFile(temp.path, "b.bin", 2);
  auto const f3 = createSizedFile(temp.path, "c.bin", 2);
  auto const f4 = createSizedFile(temp.path, "d.bin", 2);
  auto const f5 = createSizedFile(temp.path, "e.bin", 2);

  auto const grouped = pack::Packer{}.groupPackFilesWithSubparts(
    {
      PackGroupInput{f1, temp.path},
      PackGroupInput{f2, temp.path},
      PackGroupInput{f3, temp.path},
      PackGroupInput{f4, temp.path},
      PackGroupInput{f5, temp.path},
    },
    5,
    4
  );

  REQUIRE(grouped.size() == 3);
  CHECK(grouped[0].filePaths == std::vector{f1, f2});
  CHECK(grouped[0].partIndex == 1);
  CHECK(grouped[0].subPartIndex == 0);

  CHECK(grouped[1].filePaths == std::vector{f3, f4});
  CHECK(grouped[1].partIndex == 1);
  CHECK(grouped[1].subPartIndex == 1);

  CHECK(grouped[2].filePaths == std::vector{f5});
  CHECK(grouped[2].partIndex == 2);
  CHECK(grouped[2].subPartIndex == 0);
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
    pack::Packer{}.packFilesToZip({f1, f2}, zipPath, progressCtx, "Packing: bundle.zip");

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
    pack::Packer{}.packFilesToZip({f1, f2}, zipPath, progressCtx, "Packing: bundle.zip");

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
    std::ranges::count_if(
      entryNames,
      [](std::string const& name) {
        return name != "same.txt" && name.starts_with("same__") && name.ends_with(".txt");
      }
    )
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

  auto const result = pack::Packer{}.packFilesToZip(
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
  "packFilesToZip can add the same source file under multiple entry names",
  "[packer][packFilesToZip]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);
  fs::create_directories(outDir);

  auto const source = createSizedFile(srcDir, "same.txt", 64);
  auto const zipPath = outDir / "bundle.zip";

  progress::ProgressContext progressCtx;

  auto const result = pack::Packer{}.packFilesToZip(
    {
      pack::PackFileEntry{
        .sourcePath = source,
        .zipEntryName = "0000__summary__a__same.txt"
      },
      pack::PackFileEntry{.sourcePath = source, .zipEntryName = "1000__same.txt"},
    },
    zipPath,
    progressCtx,
    "Packing: bundle.zip"
  );

  REQUIRE(result);

  libzippp::ZipArchive zip{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  auto const entries = zip.getEntries();
  REQUIRE(entries.size() == 2);
  auto entryNames = std::vector<std::string>{};
  entryNames.reserve(entries.size());
  for (auto const& entry: entries) { entryNames.emplace_back(entry.getName()); }
  std::ranges::sort(entryNames);

  CHECK(
    entryNames == std::vector<std::string>{"0000__summary__a__same.txt", "1000__same.txt"}
  );
  zip.close();
}

TEST_CASE(
  "packFilesToZip uses named spinner function instead of inline lambda",
  "[packer][packFilesToZip]"
) {
  TempDir temp;
  auto const srcDir = temp.path / "src";
  auto const outDir = temp.path / "out";
  fs::create_directories(srcDir);
  fs::create_directories(outDir);

  auto const f1 = createSizedFile(srcDir, "a.txt", 64);
  auto const zipPath = outDir / "bundle.zip";

  progress::ProgressContext progressCtx;

  auto const result =
    pack::Packer{}.packFilesToZip({f1}, zipPath, progressCtx, "Packing: bundle.zip");

  REQUIRE(result);
  REQUIRE(fs::exists(zipPath));

  libzippp::ZipArchive zip{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  auto const entries = zip.getEntries();
  REQUIRE(entries.size() == 1);
  CHECK(entries.front().getName() == f1.filename().string());
  zip.close();
}

TEST_CASE("runDirectoryPackWorkflow packs directory", "[packer][workflow]") {
  TempDir temp;
  auto const inputDir = temp.path / "input";
  fs::create_directories(inputDir);
  createSizedFile(inputDir, "a.bin", 32);

  auto ctx = appctx::AppContext{};
  ctx.config.inputPath = inputDir;

  pack::PackService s;
  auto const runRes = s.runDirectoryPackWorkflow(ctx, inputDir);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK(fs::exists(inputDir / "packed" / "input_part1[1~1#1p].zip"));
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

  pack::PackService s;
  auto const packRes = s.packAllFilesInDirectory(inputDir, outputDir, 300, true);

  REQUIRE(packRes);
  REQUIRE(fs::exists(outputDir / "input_part1[1~2#2p].zip"));
  REQUIRE(fs::exists(outputDir / "input_part2[3~3#1p].zip"));

  libzippp::ZipArchive zip1{(outputDir / "input_part1[1~2#2p].zip").string()};
  zip1.open(libzippp::ZipArchive::ReadOnly);
  CHECK(zip1.getEntries().size() == 2);
  zip1.close();

  libzippp::ZipArchive zip2{(outputDir / "input_part2[3~3#1p].zip").string()};
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

  pack::PackService s;
  auto const packRes = s.packAllFilesInDirectory(inputDir, outputDir, 300, false);

  REQUIRE(packRes);
  libzippp::ZipArchive zip{(outputDir / "input_part1[1~1#1p].zip").string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  auto const entries = zip.getEntries();
  REQUIRE(entries.size() == 1);
  CHECK(entries.front().getName() == topFile.filename().string());
  CHECK(entries.front().getName() != nestedFile.filename().string());
  zip.close();
}
