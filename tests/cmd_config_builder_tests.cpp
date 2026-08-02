#include "cmd/cmd.h"
#include "cmd/config_builder.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using testutils::writeFile;

namespace {

// Helper: create a default CmdParseResult with all defaults, then override specific fields
auto makeResult(
  std::optional<std::string> input = std::nullopt,
  std::optional<std::vector<std::string>> inputs = std::nullopt,
  std::optional<std::string> output = std::nullopt,
  std::optional<std::string> stateFile = std::nullopt,
  std::string processType = "video",
  std::string outputFormat = "mp4",
  std::string forceConflictHandling = "y",
  std::string color = "auto",
  std::optional<std::size_t> maxJobs = std::nullopt,
  std::vector<std::string> flags = {},
  std::optional<int> imageQuality = std::nullopt
) -> CmdParseResult {
  auto result = CmdParseResult{};
  result.input = std::move(input);
  result.inputs = std::move(inputs);
  result.output = std::move(output);
  result.stateFile = std::move(stateFile);
  result.processType = std::move(processType);
  result.outputFormat = std::move(outputFormat);
  result.forceConflictHandling = std::move(forceConflictHandling);
  result.color = std::move(color);
  result.maxJobs = maxJobs;
  result.imageQuality = imageQuality;

  // Process flag list
  for (auto const& flag: flags) {
    if (flag == "yes") result.yesToAll = true;
    else if (flag == "recursive") result.recursive = true;
    else if (flag == "pack") result.pack = true;
    else if (flag == "pack-only") result.packOnly = true;
    else if (flag == "resume") result.resume = true;
    else if (flag == "restart") result.restart = true;
    else if (flag == "keep") result.keep = true;
    else if (flag == "folder-summary") result.folderSummary = true;
    else if (flag == "verbose") result.verbose = true;
    else if (flag == "compress") result.compress = true;
    else if (flag == "full-progress") result.fullProgress = true;
    else if (flag == "overwrite") result.overwrite = true;
    else if (flag == "help") result.help = true;
  }

  return result;
}

}  // namespace

TEST_CASE("buildConfig uses defaults when only input is provided", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result = makeResult(inputPath.string());
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  auto const config = configRes.value();
  CHECK(config.processType == "video");
  CHECK(config.outputFormat == "mp4");
  CHECK_FALSE(config.yesToAll);
  CHECK_FALSE(config.recursive);
  CHECK_FALSE(config.packOutput);
  CHECK_FALSE(config.packOnly);
  CHECK_FALSE(config.resumeState);
  CHECK_FALSE(config.restartState);
  CHECK(config.forceNameConflictHandling);
  CHECK_FALSE(config.pictureFolderSummary);
  CHECK_FALSE(config.verbose);
  CHECK(config.outputLayout == appctx::OutputLayout::Flat);
  CHECK(config.inputPath == inputPath);
  CHECK(config.inputPath.is_absolute());
}

TEST_CASE("buildConfig reads keep output layout", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "video",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {"keep"}
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  CHECK(configRes->outputLayout == appctx::OutputLayout::Keep);
}

TEST_CASE("buildConfig reads forced conflict handling flag", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result = makeResult(inputPath.string());
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  CHECK(configRes->forceNameConflictHandling);
}

TEST_CASE("buildConfig reads disabled conflict handling flag", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "video",
    "mp4",
    "n"
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  CHECK_FALSE(configRes->forceNameConflictHandling);
}

TEST_CASE("buildConfig treats preset auto as unset", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto result = makeResult(inputPath.string());
  result.nvencPreset = "auto";
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  CHECK_FALSE(configRes->nvencPreset.has_value());
}

TEST_CASE("buildConfig reads enabled folder summary flag", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "video",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {"folder-summary"}
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  CHECK(configRes->pictureFolderSummary);
}

TEST_CASE("buildConfig rejects invalid conflict-handling value", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "video",
    "mp4",
    "maybe"
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("must be set to y or n") != std::string::npos);
}

TEST_CASE("buildConfig reads resume restart and state-file options", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    statePath.string(),
    "video",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {"resume"}
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  CHECK(configRes->resumeState);
  CHECK_FALSE(configRes->restartState);
  REQUIRE(configRes->stateFilePath.has_value());
  CHECK(configRes->stateFilePath.value() == statePath);
}

TEST_CASE("buildConfig supports multiple inputs", "[cmd][config]") {
  TempDir temp;
  auto const inputA = temp.path / "a.mp4";
  auto const inputB = temp.path / "b.mp4";
  writeFile(inputA);
  writeFile(inputB);

  auto const result =
    makeResult(std::nullopt, std::vector<std::string>{inputA.string(), inputB.string()});
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  auto const config = configRes.value();
  CHECK(config.inputPaths.size() == 2);
  CHECK(config.inputPaths[0] == inputA);
  CHECK(config.inputPaths[1] == inputB);
  CHECK(config.inputPaths[0].is_absolute());
  CHECK(config.inputPaths[1].is_absolute());
}

TEST_CASE("buildConfig rejects both input and inputs", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result =
    makeResult(inputPath.string(), std::vector<std::string>{inputPath.string()});
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("either -i/--input or -I/--inputs") != std::string::npos);
}

TEST_CASE("buildConfig rejects invalid process type", "[cmd][config]") {
  auto const result =
    makeResult(std::nullopt, std::nullopt, std::nullopt, std::nullopt, "bad");
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("Invalid process type") != std::string::npos);
}

TEST_CASE("buildConfig maps vid alias to video", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result =
    makeResult(inputPath.string(), std::nullopt, std::nullopt, std::nullopt, "vid");
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  CHECK(configRes->processType == "video");
}

TEST_CASE("buildConfig maps pic alias to picture", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result =
    makeResult(inputPath.string(), std::nullopt, std::nullopt, std::nullopt, "pic");
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  CHECK(configRes->processType == "picture");
}

TEST_CASE("buildConfig rejects multi-input for picture type", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result = makeResult(
    std::nullopt,
    std::vector<std::string>{inputPath.string()},
    std::nullopt,
    std::nullopt,
    "picture"
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("only supported with --type video") != std::string::npos);
}

TEST_CASE("buildConfig rejects multi-input with pack-only", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result = makeResult(
    std::nullopt,
    std::vector<std::string>{inputPath.string()},
    std::nullopt,
    std::nullopt,
    "video",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {"pack-only"}
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("cannot be used with --pack-only") != std::string::npos);
}

TEST_CASE("buildConfig rejects multi-input directory path", "[cmd][config]") {
  TempDir temp;

  auto const result =
    makeResult(std::nullopt, std::vector<std::string>{temp.path.string()});
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("not a file") != std::string::npos);
}

TEST_CASE("buildConfig rejects invalid output format", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "video",
    "mkv"
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("Invalid output format") != std::string::npos);
}

TEST_CASE("buildConfig requires input path", "[cmd][config]") {
  auto const result = makeResult();
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("Input path is required") != std::string::npos);
}

TEST_CASE("buildConfig rejects missing input path", "[cmd][config]") {
  auto const result = makeResult("missing.mp4");
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("does not exist") != std::string::npos);
}

TEST_CASE("buildConfig rejects output path that is not a directory", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputFile = temp.path / "output.txt";
  writeFile(inputPath);
  writeFile(outputFile);

  auto const result = makeResult(inputPath.string(), std::nullopt, outputFile.string());
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("output path is not a directory") != std::string::npos);
}

TEST_CASE("buildConfig accepts output path that does not exist yet", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputDir = temp.path / "new_output";
  writeFile(inputPath);

  auto const result = makeResult(inputPath.string(), std::nullopt, outputDir.string());
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  REQUIRE(configRes->outputPath.has_value());
  CHECK(configRes->outputPath.value() == outputDir);
}

TEST_CASE(
  "buildConfig resolves + alias to single-file input directory",
  "[cmd][config]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result = makeResult(inputPath.string(), std::nullopt, "+");
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  REQUIRE(configRes->outputPath.has_value());
  CHECK(configRes->outputPath.value() == temp.path);
}

TEST_CASE("buildConfig resolves input alias with relative suffix", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result =
    makeResult(inputPath.string(), std::nullopt, "input://encoded/webp");
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  REQUIRE(configRes->outputPath.has_value());
  CHECK(configRes->outputPath.value() == temp.path / "encoded" / "webp");
}

TEST_CASE(
  "buildConfig resolves = alias to nearest common ancestor for multi-input",
  "[cmd][config]"
) {
  TempDir temp;
  auto const inputA = temp.path / "group_a" / "clip_a.mp4";
  auto const inputB = temp.path / "group_b" / "clip_b.mp4";
  fs::create_directories(inputA.parent_path());
  fs::create_directories(inputB.parent_path());
  writeFile(inputA);
  writeFile(inputB);

  auto const result = makeResult(
    std::nullopt,
    std::vector<std::string>{inputA.string(), inputB.string()},
    "=/encoded"
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  REQUIRE(configRes->outputPath.has_value());
  CHECK(configRes->outputPath.value() == temp.path / "encoded");
}

TEST_CASE(
  "buildConfig rejects input alias for multi-input without shared parent",
  "[cmd][config]"
) {
  TempDir temp;
  auto const inputA = temp.path / "group_a" / "clip_a.mp4";
  auto const inputB = temp.path / "group_b" / "clip_b.mp4";
  fs::create_directories(inputA.parent_path());
  fs::create_directories(inputB.parent_path());
  writeFile(inputA);
  writeFile(inputB);

  auto const result = makeResult(
    std::nullopt,
    std::vector<std::string>{inputA.string(), inputB.string()},
    "+/encoded"
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("share the same parent directory") != std::string::npos);
}

TEST_CASE(
  "buildConfig resolves input alias for multi-input with shared parent",
  "[cmd][config]"
) {
  TempDir temp;
  auto const sharedDir = temp.path / "shared";
  auto const inputA = sharedDir / "clip_a.mp4";
  auto const inputB = sharedDir / "clip_b.mp4";
  fs::create_directories(sharedDir);
  writeFile(inputA);
  writeFile(inputB);

  auto const result = makeResult(
    std::nullopt,
    std::vector<std::string>{inputA.string(), inputB.string()},
    "+\\encoded"
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  REQUIRE(configRes->outputPath.has_value());
  CHECK(configRes->outputPath.value() == sharedDir / "encoded");
}

TEST_CASE("buildConfig rejects ffmpeg path that is not a directory", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const ffmpegFile = temp.path / "ffmpeg";
  writeFile(inputPath);
  writeFile(ffmpegFile);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "video",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {},
    std::nullopt
  );
  // Need to set ffmpegPath manually since makeResult doesn't have it as a param
  auto overriddenResult = result;
  overriddenResult.ffmpegPath = ffmpegFile.string();
  auto const configRes = cmd::buildConfig(overriddenResult);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("FFmpeg") != std::string::npos);
}

TEST_CASE("buildConfig captures flags and paths", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputDir = temp.path / "out";
  writeFile(inputPath);
  fs::create_directories(outputDir);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    outputDir.string(),
    std::nullopt,
    "picture",
    "webp",
    "y",
    "auto",
    std::nullopt,
    {"yes", "recursive", "pack", "pack-only", "folder-summary", "verbose"}
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  auto const config = configRes.value();
  CHECK(config.processType == "picture");
  CHECK(config.outputFormat == "webp");
  CHECK(config.yesToAll);
  CHECK(config.recursive);
  CHECK(config.packOutput);
  CHECK(config.packOnly);
  CHECK(config.verbose);
  CHECK(config.pictureFolderSummary);
  CHECK(config.outputLayout == appctx::OutputLayout::Flat);
  CHECK(config.outputPath == outputDir);
}

TEST_CASE("buildConfig reads custom max parallel jobs", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "video",
    "mp4",
    "y",
    "auto",
    std::size_t{4}
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  REQUIRE(configRes->maxParallelJobs.has_value());
  CHECK(configRes->maxParallelJobs.value() == 4);
}

TEST_CASE("buildConfig rejects jobs = 0", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "video",
    "mp4",
    "y",
    "auto",
    std::size_t{0}
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("--jobs must be >= 1") != std::string::npos);
}

TEST_CASE("buildConfig enables compressImages with --compress", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "pics";
  fs::create_directories(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "picture",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {"compress"}
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  CHECK(configRes->compressImages == true);
  CHECK_FALSE(configRes->imageQuality.has_value());
}

TEST_CASE("buildConfig rejects --compress without picture type", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "video",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {"compress"}
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(
    configRes.error().find("--compress is only supported with --type picture")
    != std::string::npos
  );
}

TEST_CASE("buildConfig reads --image-quality with --compress", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "pics";
  fs::create_directories(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "picture",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {"compress"},
    10
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  CHECK(configRes->compressImages == true);
  REQUIRE(configRes->imageQuality.has_value());
  CHECK(configRes->imageQuality.value() == 10);
}

TEST_CASE("buildConfig rejects --image-quality without --compress", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "pics";
  fs::create_directories(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "picture",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {},
    10
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(
    configRes.error().find("--image-quality requires --compress") != std::string::npos
  );
}

TEST_CASE("buildConfig rejects --image-quality below 2", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "pics";
  fs::create_directories(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "picture",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {"compress"},
    1
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(
    configRes.error().find("--image-quality must be between 2 and 31")
    != std::string::npos
  );
}

TEST_CASE("buildConfig rejects --image-quality above 31", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "pics";
  fs::create_directories(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "picture",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {"compress"},
    32
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE_FALSE(configRes);
  CHECK(
    configRes.error().find("--image-quality must be between 2 and 31")
    != std::string::npos
  );
}

TEST_CASE("buildConfig accepts --image-quality at minimum 2", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "pics";
  fs::create_directories(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "picture",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {"compress"},
    2
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  REQUIRE(configRes->imageQuality.has_value());
  CHECK(configRes->imageQuality.value() == 2);
}

TEST_CASE("buildConfig accepts --image-quality at maximum 31", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "pics";
  fs::create_directories(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "picture",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {"compress"},
    31
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  REQUIRE(configRes->imageQuality.has_value());
  CHECK(configRes->imageQuality.value() == 31);
}

TEST_CASE(
  "buildConfig leaves imageQuality unset when --image-quality not provided",
  "[cmd][config]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "pics";
  fs::create_directories(inputPath);

  auto const result = makeResult(
    inputPath.string(),
    std::nullopt,
    std::nullopt,
    std::nullopt,
    "picture",
    "mp4",
    "y",
    "auto",
    std::nullopt,
    {"compress"}
  );
  auto const configRes = cmd::buildConfig(result);

  REQUIRE(configRes);
  CHECK(configRes->compressImages == true);
  CHECK_FALSE(configRes->imageQuality.has_value());
}
