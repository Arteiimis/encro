#include "core/pipeline.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>
#include <libzippp/libzippp.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

void touchFile(fs::path const& filePath) {
  std::ofstream out{filePath};
  REQUIRE(out.is_open());
  out << "x";
}

}  // namespace

TEST_CASE("pack-only pipeline rejects non-video type", "[pipeline]") {
  TempDir temp;
  auto ctx = appctx::AppContext{};
  ctx.config.packOnly = true;
  ctx.config.processType = "picture";
  ctx.config.inputPath = temp.path;

  auto runRes = pipeline::run(ctx);
  REQUIRE_FALSE(runRes);
  CHECK(runRes.error().find("pack-only option") != std::string::npos);
}

TEST_CASE("pack-only pipeline packs directory", "[pipeline]") {
  TempDir temp;
  auto const inputDir = temp.path / "input";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.bin");

  auto ctx = appctx::AppContext{};
  ctx.config.packOnly = true;
  ctx.config.processType = "video";
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK(fs::exists(inputDir / "packed" / "input_part1_1-1_items1.zip"));
}

TEST_CASE("picture pipeline packs directory", "[pipeline]") {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK(fs::exists(inputDir / "packed" / "pics_part1_1-1_items1.zip"));
}

TEST_CASE(
  "pack-only pipeline defaults to collision-safe file names",
  "[pipeline]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "input";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "alpha.bin");
  touchFile(dirB / "beta.bin");

  auto ctx = appctx::AppContext{};
  ctx.config.packOnly = true;
  ctx.config.processType = "video";
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto const zipPath = inputDir / "packed" / "input_part1_1-2_items2.zip";
  REQUIRE(fs::exists(zipPath));

  libzippp::ZipArchive zip{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  auto const entries = zip.getEntries();
  auto entryNames = std::vector<std::string>{};
  entryNames.reserve(entries.size());
  for (auto const& entry: entries) {
    if (entry.getName().ends_with('/')) { continue; }
    entryNames.emplace_back(entry.getName());
  }
  std::ranges::sort(entryNames);

  REQUIRE(entryNames.size() == 2);
  CHECK(entryNames[0].starts_with("a__alpha__"));
  CHECK(entryNames[1].starts_with("b__beta__"));
  zip.close();
}

TEST_CASE(
  "pack-only pipeline can disable collision-safe file names",
  "[pipeline]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "input";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "alpha.bin");
  touchFile(dirB / "beta.bin");

  auto ctx = appctx::AppContext{};
  ctx.config.packOnly = true;
  ctx.config.processType = "video";
  ctx.config.forceNameConflictHandling = false;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto const zipPath = inputDir / "packed" / "input_part1_1-2_items2.zip";
  REQUIRE(fs::exists(zipPath));

  libzippp::ZipArchive zip{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  auto const entries = zip.getEntries();
  auto entryNames = std::vector<std::string>{};
  entryNames.reserve(entries.size());
  for (auto const& entry: entries) {
    if (entry.getName().ends_with('/')) { continue; }
    entryNames.emplace_back(entry.getName());
  }
  std::ranges::sort(entryNames);

  REQUIRE(entryNames.size() == 2);
  CHECK(entryNames == std::vector<std::string>{"alpha.bin", "beta.bin"});
  zip.close();
}

TEST_CASE(
  "picture pipeline keeps same-folder files grouped in flat mode",
  "[pipeline]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "alpha.jpg");
  touchFile(dirA / "beta.jpg");
  touchFile(dirB / "alpha.jpg");
  touchFile(dirB / "beta.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto const zipPath = inputDir / "packed" / "pics_part1_1-4_items4.zip";
  REQUIRE(fs::exists(zipPath));

  libzippp::ZipArchive zip{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  auto const entries = zip.getEntries();
  auto entryNames = std::vector<std::string>{};
  entryNames.reserve(entries.size());
  for (auto const& entry: entries) {
    if (entry.getName().ends_with('/')) { continue; }
    entryNames.emplace_back(entry.getName());
  }
  std::ranges::sort(entryNames);

  REQUIRE(entryNames.size() == 4);
  CHECK(entryNames[0].starts_with("a__alpha__"));
  CHECK(entryNames[1].starts_with("a__beta__"));
  CHECK(entryNames[2].starts_with("b__alpha__"));
  CHECK(entryNames[3].starts_with("b__beta__"));
  zip.close();
}

TEST_CASE(
  "picture pipeline defaults to collision-safe names for unique files",
  "[pipeline]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "alpha.jpg");
  touchFile(dirB / "beta.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto const zipPath = inputDir / "packed" / "pics_part1_1-2_items2.zip";
  REQUIRE(fs::exists(zipPath));

  libzippp::ZipArchive zip{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  auto const entries = zip.getEntries();
  auto entryNames = std::vector<std::string>{};
  entryNames.reserve(entries.size());
  for (auto const& entry: entries) {
    if (entry.getName().ends_with('/')) { continue; }
    entryNames.emplace_back(entry.getName());
  }
  std::ranges::sort(entryNames);

  REQUIRE(entryNames.size() == 2);
  CHECK(entryNames[0].starts_with("a__alpha__"));
  CHECK(entryNames[1].starts_with("b__beta__"));
  zip.close();
}

TEST_CASE(
  "picture pipeline can disable collision-safe names for unique files",
  "[pipeline]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "alpha.jpg");
  touchFile(dirB / "beta.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.forceNameConflictHandling = false;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto const zipPath = inputDir / "packed" / "pics_part1_1-2_items2.zip";
  REQUIRE(fs::exists(zipPath));

  libzippp::ZipArchive zip{zipPath.string()};
  zip.open(libzippp::ZipArchive::ReadOnly);
  auto const entries = zip.getEntries();
  auto entryNames = std::vector<std::string>{};
  entryNames.reserve(entries.size());
  for (auto const& entry: entries) {
    if (entry.getName().ends_with('/')) { continue; }
    entryNames.emplace_back(entry.getName());
  }
  std::ranges::sort(entryNames);

  REQUIRE(entryNames.size() == 2);
  CHECK(entryNames == std::vector<std::string>{"alpha.jpg", "beta.jpg"});
  zip.close();
}

TEST_CASE("picture pipeline keeps relative paths in keep mode", "[pipeline]") {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "a";
  auto const dirB = inputDir / "b";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "same.jpg");
  touchFile(dirB / "same.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.recursive = true;
  ctx.config.outputLayout = appctx::OutputLayout::Keep;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto const zipPath = inputDir / "packed" / "pics_part1_1-2_items2.zip";
  REQUIRE(fs::exists(zipPath));

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

  CHECK(entryNames == std::vector<std::string>{"a/same.jpg", "b/same.jpg"});
  zip.close();
}
