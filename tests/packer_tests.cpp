#include "packer.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>
#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>
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

  DynamicProgress<ProgressBar> progressManager;
  ProgressBar bar(option::MaxProgress{100});
  progressManager.push_back(bar);

  auto const result = packFilesToZip({f1, f2}, zipPath, progressManager, 0);

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
