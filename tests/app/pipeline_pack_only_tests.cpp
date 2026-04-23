#include "app/pipeline.h"
#include "core/job_state.h"
#include "infra/stop_signal.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <filesystem>

namespace fs = std::filesystem;

using testutils::collisionGroupPrefix;
using testutils::hasCollisionSafePrefix;
using testutils::listZipRegularEntryNames;
using testutils::ScopedStopSignalReset;
using testutils::touchFile;

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
  CHECK(fs::exists(inputDir / "packed" / "input_part1[1~1#1p].zip"));
}

TEST_CASE("pack-only pipeline skips job state by default", "[pipeline]") {
  TempDir temp;
  auto const inputDir = temp.path / "input";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.bin");

  auto ctx = appctx::AppContext{};
  ctx.config.packOnly = true;
  ctx.config.processType = "video";
  ctx.config.inputPath = inputDir;

  auto const stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config);
  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK_FALSE(fs::exists(stateFilePath));
}

TEST_CASE(
  "pack-only pipeline marks pending archive task interrupted when canceled with job "
  "state",
  "[pipeline]"
) {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const inputDir = temp.path / "input";
  auto const stateFilePath = temp.path / "state.json";
  fs::create_directories(inputDir);
  touchFile(inputDir / "a.bin");

  auto ctx = appctx::AppContext{};
  ctx.config.packOnly = true;
  ctx.config.processType = "video";
  ctx.config.inputPath = inputDir;
  ctx.config.stateFilePath = stateFilePath;

  stopsignal::requestStop();

  auto runRes = pipeline::run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == stopsignal::kCanceledExitCode);

  REQUIRE(ctx.runtime.jobState != nullptr);
  CHECK(ctx.runtime.jobState->isCancelRequested());

  auto const tasks = ctx.runtime.jobState->tasks();
  REQUIRE(tasks.size() == 1);
  CHECK(tasks.front().kind == std::string{jobstate::kBuildArchiveKind});
  CHECK(tasks.front().status == jobstate::TaskStatus::Interrupted);
  CHECK(tasks.front().lastError == std::optional<std::string>{"canceled by user"});
  CHECK_FALSE(fs::exists(inputDir / "packed" / "input_part1[1~1#1p].zip"));
}

TEST_CASE("pack-only pipeline defaults to collision-safe file names", "[pipeline]") {
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

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "input_part1[1~2#2p].zip");

  REQUIRE(entryNames.size() == 2);
  CHECK(hasCollisionSafePrefix(entryNames[0], "a", "alpha"));
  CHECK(hasCollisionSafePrefix(entryNames[1], "b", "beta"));
}

TEST_CASE("pack-only pipeline can disable collision-safe file names", "[pipeline]") {
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

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "input_part1[1~2#2p].zip");

  REQUIRE(entryNames.size() == 2);
  CHECK(entryNames == std::vector<std::string>{"alpha.bin", "beta.bin"});
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

  auto const entryNames =
    listZipRegularEntryNames(inputDir / "packed" / "input_part1[1~4#4p].zip");

  REQUIRE(entryNames.size() == 4);
  CHECK(collisionGroupPrefix(entryNames[0]) == collisionGroupPrefix(entryNames[1]));
  CHECK(collisionGroupPrefix(entryNames[2]) == collisionGroupPrefix(entryNames[3]));
  CHECK(collisionGroupPrefix(entryNames[0]) != collisionGroupPrefix(entryNames[2]));
  CHECK(entryNames[0].find("__alpha__") != std::string::npos);
  CHECK(entryNames[1].find("__beta__") != std::string::npos);
  CHECK(entryNames[2].find("__alpha__") != std::string::npos);
  CHECK(entryNames[3].find("__beta__") != std::string::npos);
}
