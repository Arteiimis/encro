#include "core/pipeline.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

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

  auto pipelineRes = pipeline::selectPipeline(ctx);
  REQUIRE(pipelineRes);

  auto runRes = pipelineRes.value()->run(ctx);
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

  auto pipelineRes = pipeline::selectPipeline(ctx);
  REQUIRE(pipelineRes);

  auto runRes = pipelineRes.value()->run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK(fs::exists(inputDir / "packed" / "input_part1.zip"));
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

  auto pipelineRes = pipeline::selectPipeline(ctx);
  REQUIRE(pipelineRes);

  auto runRes = pipelineRes.value()->run(ctx);
  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK(fs::exists(inputDir / "packed" / "pics_part1.zip"));
}
