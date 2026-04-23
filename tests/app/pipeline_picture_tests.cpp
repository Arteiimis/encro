#include "app/pipeline.h"
#include "core/job_state.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <filesystem>

namespace fs = std::filesystem;

using testutils::collisionGroupPrefix;
using testutils::hasCollisionSafePrefix;
using testutils::listZipRegularEntryNames;
using testutils::touchFile;

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
  CHECK(fs::exists(inputDir / "packed" / "pics_part1[1~1#1p].zip"));
}

TEST_CASE("picture pipeline skips job state by default", "[pipeline]") {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.jpg");

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "picture";
  ctx.config.yesToAll = true;
  ctx.config.inputPath = inputDir;

  auto const stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config);
  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK_FALSE(fs::exists(stateFilePath));
}

TEST_CASE("picture pipeline keeps same-folder files grouped in flat mode", "[pipeline]") {
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
  ctx.config.pictureFolderSummary = false;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "pics_part1[1~4#4p].zip");

  REQUIRE(entryNames.size() == 4);
  CHECK(hasCollisionSafePrefix(entryNames[0], "a", "alpha"));
  CHECK(hasCollisionSafePrefix(entryNames[1], "a", "beta"));
  CHECK(hasCollisionSafePrefix(entryNames[2], "b", "alpha"));
  CHECK(hasCollisionSafePrefix(entryNames[3], "b", "beta"));
}

TEST_CASE(
  "picture pipeline adds flat summary files ahead of normal files by name",
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
  ctx.config.pictureFolderSummary = true;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "pics_part1[1~6#6p].zip");

  REQUIRE(entryNames.size() == 6);
  CHECK(entryNames[0].starts_with("0000__summary__"));
  CHECK(entryNames[1].starts_with("0000__summary__"));
  CHECK(entryNames[2].starts_with("1000__"));
  CHECK(entryNames[3].starts_with("1000__"));
  CHECK(entryNames[4].starts_with("1000__"));
  CHECK(entryNames[5].starts_with("1000__"));
  CHECK(std::ranges::none_of(entryNames, [](std::string const& name) {
    return name.find('/') != std::string::npos;
  }));
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
  ctx.config.pictureFolderSummary = false;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "pics_part1[1~2#2p].zip");

  REQUIRE(entryNames.size() == 2);
  CHECK(hasCollisionSafePrefix(entryNames[0], "a", "alpha"));
  CHECK(hasCollisionSafePrefix(entryNames[1], "b", "beta"));
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
  ctx.config.pictureFolderSummary = false;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "pics_part1[1~4#4p].zip");

  REQUIRE(entryNames.size() == 4);
  CHECK(collisionGroupPrefix(entryNames[0]) == collisionGroupPrefix(entryNames[1]));
  CHECK(collisionGroupPrefix(entryNames[2]) == collisionGroupPrefix(entryNames[3]));
  CHECK(collisionGroupPrefix(entryNames[0]) != collisionGroupPrefix(entryNames[2]));
  CHECK(entryNames[0].find("__alpha__") != std::string::npos);
  CHECK(entryNames[1].find("__beta__") != std::string::npos);
  CHECK(entryNames[2].find("__alpha__") != std::string::npos);
  CHECK(entryNames[3].find("__beta__") != std::string::npos);
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
  ctx.config.pictureFolderSummary = false;
  ctx.config.forceNameConflictHandling = false;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "pics_part1[1~2#2p].zip");

  REQUIRE(entryNames.size() == 2);
  CHECK(entryNames == std::vector<std::string>{"1000__alpha.jpg", "1000__beta.jpg"});
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
  ctx.config.pictureFolderSummary = false;
  ctx.config.outputLayout = appctx::OutputLayout::Keep;
  ctx.config.inputPath = inputDir;

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  REQUIRE(runRes.value() == 0);

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "pics_part1[1~2#2p].zip");

  REQUIRE(entryNames.size() == 2);
  CHECK(entryNames == std::vector<std::string>{"a/same.jpg", "b/same.jpg"});
}
