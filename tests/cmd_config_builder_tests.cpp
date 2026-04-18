#include "cmd/config_builder.h"
#include "test_utils.h"

#include <boost/program_options/variables_map.hpp>
#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

auto writeFile(fs::path const& path) -> void {
  std::ofstream out{path};
  REQUIRE(out.is_open());
  out << "x";
}

auto makeVm(
  std::vector<std::pair<std::string, std::string>> const& options,
  std::vector<std::string> const& flags = {}
) -> boost::program_options::variables_map {
  namespace po = boost::program_options;

  auto vm = po::variables_map{};
  for (auto const& [key, value]: options) {
    vm.insert({key, po::variable_value(boost::any{value}, false)});
  }
  for (auto const& flag: flags) {
    vm.insert({flag, po::variable_value(boost::any{}, false)});
  }

  return vm;
}

}  // namespace

TEST_CASE("buildConfig uses defaults when only input is provided", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const vm = makeVm({{"input", inputPath.string()}});
  auto const configRes = cmd::buildConfig(vm);

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
  CHECK_FALSE(config.verbose);
  CHECK_FALSE(config.verboseEcho);
  CHECK(config.outputLayout == appctx::OutputLayout::Flat);
  CHECK(config.inputPath == inputPath);
  CHECK(config.inputPath.is_absolute());
}

TEST_CASE("buildConfig reads keep output layout", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const vm = makeVm({{"input", inputPath.string()}}, {"keep"});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE(configRes);
  CHECK(configRes->outputLayout == appctx::OutputLayout::Keep);
}

TEST_CASE("buildConfig reads forced conflict handling flag", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const vm =
    makeVm({{"input", inputPath.string()}, {"force-conflict-handling", "y"}});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE(configRes);
  CHECK(configRes->forceNameConflictHandling);
}

TEST_CASE("buildConfig reads disabled conflict handling flag", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const vm =
    makeVm({{"input", inputPath.string()}, {"force-conflict-handling", "n"}});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE(configRes);
  CHECK_FALSE(configRes->forceNameConflictHandling);
}

TEST_CASE("buildConfig rejects invalid conflict-handling value", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const vm =
    makeVm({{"input", inputPath.string()}, {"force-conflict-handling", "maybe"}});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("must be set to y or n") != std::string::npos);
}

TEST_CASE("buildConfig rejects conflicting output layouts", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const vm = makeVm({{"input", inputPath.string()}}, {"flat", "keep"});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("--flat and --keep") != std::string::npos);
}

TEST_CASE("buildConfig reads resume restart and state-file options", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  writeFile(inputPath);

  auto const vm = makeVm(
    {{"input", inputPath.string()}, {"state-file", statePath.string()}},
    {"resume"}
  );
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE(configRes);
  CHECK(configRes->resumeState);
  CHECK_FALSE(configRes->restartState);
  REQUIRE(configRes->stateFilePath.has_value());
  CHECK(configRes->stateFilePath.value() == statePath);
}

TEST_CASE("buildConfig rejects conflicting resume and restart", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const vm = makeVm({{"input", inputPath.string()}}, {"resume", "restart"});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("--resume and --restart") != std::string::npos);
}

TEST_CASE("buildConfig supports multiple inputs", "[cmd][config]") {
  namespace po = boost::program_options;

  TempDir temp;
  auto const inputA = temp.path / "a.mp4";
  auto const inputB = temp.path / "b.mp4";
  writeFile(inputA);
  writeFile(inputB);

  auto vm = makeVm({{"type", "video"}});
  vm.insert(
    {"inputs",
     po::variable_value(
       boost::any{std::vector<std::string>{inputA.string(), inputB.string()}},
       false
     )}
  );

  auto const configRes = cmd::buildConfig(vm);

  REQUIRE(configRes);
  auto const config = configRes.value();
  CHECK(config.inputPaths.size() == 2);
  CHECK(config.inputPaths[0] == inputA);
  CHECK(config.inputPaths[1] == inputB);
  CHECK(config.inputPaths[0].is_absolute());
  CHECK(config.inputPaths[1].is_absolute());
}

TEST_CASE("buildConfig rejects both input and inputs", "[cmd][config]") {
  namespace po = boost::program_options;

  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto vm = makeVm({{"input", inputPath.string()}, {"type", "video"}});
  vm.insert(
    std::pair{
      "inputs",
      po::variable_value(
        boost::any{std::vector<std::string>{inputPath.string()}},
        false
      )
    }
  );

  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(
    configRes.error().find("either -i/--input or -I/--inputs") != std::string::npos
  );
}

TEST_CASE("buildConfig rejects invalid process type", "[cmd][config]") {
  auto const vm = makeVm({{"type", "bad"}});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("Invalid process type") != std::string::npos);
}

TEST_CASE("buildConfig maps vid alias to video", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const vm = makeVm({{"input", inputPath.string()}, {"type", "vid"}});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE(configRes);
  CHECK(configRes->processType == "video");
}

TEST_CASE("buildConfig maps pic alias to picture", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const vm = makeVm({{"input", inputPath.string()}, {"type", "pic"}});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE(configRes);
  CHECK(configRes->processType == "picture");
}

TEST_CASE("buildConfig rejects multi-input for picture type", "[cmd][config]") {
  namespace po = boost::program_options;

  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto vm = makeVm({{"type", "picture"}});
  vm.insert(
    {"inputs",
     po::variable_value(
       boost::any{std::vector<std::string>{inputPath.string()}},
       false
     )}
  );

  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("only supported for video") != std::string::npos);
}

TEST_CASE("buildConfig rejects multi-input with pack-only", "[cmd][config]") {
  namespace po = boost::program_options;

  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto vm = makeVm({{"type", "video"}}, {"pack-only"});
  vm.insert(
    {"inputs",
     po::variable_value(
       boost::any{std::vector<std::string>{inputPath.string()}},
       false
     )}
  );

  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("not supported with pack-only") != std::string::npos);
}

TEST_CASE("buildConfig rejects multi-input directory path", "[cmd][config]") {
  namespace po = boost::program_options;

  TempDir temp;

  auto vm = makeVm({{"type", "video"}});
  vm.insert(
    {"inputs",
     po::variable_value(
       boost::any{std::vector<std::string>{temp.path.string()}},
       false
     )}
  );

  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("not a file") != std::string::npos);
}

TEST_CASE("buildConfig rejects invalid output format", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const vm = makeVm({{"output-format", "mkv"}, {"input", inputPath.string()}});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("Invalid output format") != std::string::npos);
}

TEST_CASE("buildConfig requires input path", "[cmd][config]") {
  auto const vm = makeVm({});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("Input path is required") != std::string::npos);
}

TEST_CASE("buildConfig rejects missing input path", "[cmd][config]") {
  auto const vm = makeVm({{"input", "missing.mp4"}});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("does not exist") != std::string::npos);
}

TEST_CASE(
  "buildConfig rejects output path that is not a directory",
  "[cmd][config]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputFile = temp.path / "output.txt";
  writeFile(inputPath);
  writeFile(outputFile);

  auto const vm =
    makeVm({{"input", inputPath.string()}, {"output", outputFile.string()}});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(
    configRes.error().find("output path is not a directory") != std::string::npos
  );
}

TEST_CASE(
  "buildConfig rejects ffmpeg path that is not a directory",
  "[cmd][config]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const ffmpegFile = temp.path / "ffmpeg";
  writeFile(inputPath);
  writeFile(ffmpegFile);

  auto const vm =
    makeVm({{"input", inputPath.string()}, {"ffmpeg-path", ffmpegFile.string()}});
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("FFmpeg") != std::string::npos);
}

TEST_CASE("buildConfig captures flags and paths", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  auto const outputDir = temp.path / "out";
  writeFile(inputPath);
  fs::create_directories(outputDir);

  auto const vm = makeVm(
    {{"input", inputPath.string()},
     {"output", outputDir.string()},
     {"type", "picture"},
     {"output-format", "webp"}},
    {"yes", "recursive", "pack", "pack-only", "verbose", "verbose-echo"}
  );
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE(configRes);
  auto const config = configRes.value();
  CHECK(config.processType == "picture");
  CHECK(config.outputFormat == "webp");
  CHECK(config.yesToAll);
  CHECK(config.recursive);
  CHECK(config.packOutput);
  CHECK(config.packOnly);
  CHECK(config.verbose);
  CHECK(config.verboseEcho);
  CHECK(config.outputLayout == appctx::OutputLayout::Flat);
  CHECK(config.outputPath == outputDir);
}

TEST_CASE("buildConfig reads custom max parallel jobs", "[cmd][config]") {
  namespace po = boost::program_options;

  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto vm = makeVm({{"input", inputPath.string()}});
  vm.insert({"jobs", po::variable_value(boost::any{std::size_t{4}}, false)});

  auto const configRes = cmd::buildConfig(vm);

  REQUIRE(configRes);
  REQUIRE(configRes->maxParallelJobs.has_value());
  CHECK(configRes->maxParallelJobs.value() == 4);
}

TEST_CASE("buildConfig rejects jobs = 0", "[cmd][config]") {
  namespace po = boost::program_options;

  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto vm = makeVm({{"input", inputPath.string()}});
  vm.insert({"jobs", po::variable_value(boost::any{std::size_t{0}}, false)});

  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("--jobs must be >= 1") != std::string::npos);
}
