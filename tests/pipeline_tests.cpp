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

auto hasCollisionSafePrefix(
  std::string const& entryName,
  std::string_view dirLabel,
  std::string_view stem
) -> bool {
  return entryName.starts_with(std::format("{}__", dirLabel))
      && entryName.find(std::format("__{}__", stem)) != std::string::npos;
}

auto collisionGroupPrefix(std::string const& entryName) -> std::string {
  auto const lastSep = entryName.rfind("__");
  if (lastSep == std::string::npos) { return entryName; }
  auto const stemSep = entryName.rfind("__", lastSep - 1);
  if (stemSep == std::string::npos) { return entryName.substr(0, lastSep); }
  return entryName.substr(0, stemSep);
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
  CHECK(hasCollisionSafePrefix(entryNames[0], "a", "alpha"));
  CHECK(hasCollisionSafePrefix(entryNames[1], "b", "beta"));
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
  "pack-only pipeline keeps weakly-sanitized directory names grouped",
  "[pipeline]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "input";
  auto const dirA = inputDir / "丹花イブキ(110p + 音声あり動画)";
  auto const dirB = inputDir / "天川そら(110p + 音声あり動画)";
  fs::create_directories(dirA);
  fs::create_directories(dirB);
  touchFile(dirA / "alpha.bin");
  touchFile(dirA / "beta.bin");
  touchFile(dirB / "alpha.bin");
  touchFile(dirB / "beta.bin");

  auto ctx = appctx::AppContext{};
  ctx.config.packOnly = true;
  ctx.config.processType = "video";
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto const zipPath = inputDir / "packed" / "input_part1_1-4_items4.zip";
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
  CHECK(collisionGroupPrefix(entryNames[0]) == collisionGroupPrefix(entryNames[1]));
  CHECK(collisionGroupPrefix(entryNames[2]) == collisionGroupPrefix(entryNames[3]));
  CHECK(collisionGroupPrefix(entryNames[0]) != collisionGroupPrefix(entryNames[2]));
  CHECK(entryNames[0].find("__alpha__") != std::string::npos);
  CHECK(entryNames[1].find("__beta__") != std::string::npos);
  CHECK(entryNames[2].find("__alpha__") != std::string::npos);
  CHECK(entryNames[3].find("__beta__") != std::string::npos);
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
  CHECK(hasCollisionSafePrefix(entryNames[0], "a", "alpha"));
  CHECK(hasCollisionSafePrefix(entryNames[1], "a", "beta"));
  CHECK(hasCollisionSafePrefix(entryNames[2], "b", "alpha"));
  CHECK(hasCollisionSafePrefix(entryNames[3], "b", "beta"));
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
  CHECK(hasCollisionSafePrefix(entryNames[0], "a", "alpha"));
  CHECK(hasCollisionSafePrefix(entryNames[1], "b", "beta"));
  zip.close();
}

TEST_CASE(
  "picture pipeline keeps weakly-sanitized directory names grouped",
  "[pipeline]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const dirA = inputDir / "丹花イブキ(110p + 音声あり動画)";
  auto const dirB = inputDir / "天川そら(110p + 音声あり動画)";
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
  CHECK(collisionGroupPrefix(entryNames[0]) == collisionGroupPrefix(entryNames[1]));
  CHECK(collisionGroupPrefix(entryNames[2]) == collisionGroupPrefix(entryNames[3]));
  CHECK(collisionGroupPrefix(entryNames[0]) != collisionGroupPrefix(entryNames[2]));
  CHECK(entryNames[0].find("__alpha__") != std::string::npos);
  CHECK(entryNames[1].find("__beta__") != std::string::npos);
  CHECK(entryNames[2].find("__alpha__") != std::string::npos);
  CHECK(entryNames[3].find("__beta__") != std::string::npos);
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
