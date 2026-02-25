#include "cmd/config_builder.h"
#include "test_utils.h"

#include <boost/any.hpp>
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

  auto const vm = makeVm({
    {"input", inputPath.string()}
  });
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE(configRes);
  auto const config = configRes.value();
  CHECK(config.processType == "video");
  CHECK(config.outputFormat == "mp4");
  CHECK_FALSE(config.yesToAll);
  CHECK_FALSE(config.recursive);
  CHECK_FALSE(config.packOutput);
  CHECK_FALSE(config.packOnly);
  CHECK(config.inputPath == inputPath);
}

TEST_CASE("buildConfig rejects invalid process type", "[cmd][config]") {
  auto const vm = makeVm({
    {"type", "bad"}
  });
  auto const configRes = cmd::buildConfig(vm);

  REQUIRE_FALSE(configRes);
  CHECK(configRes.error().find("Invalid process type") != std::string::npos);
}

TEST_CASE("buildConfig rejects invalid output format", "[cmd][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto const vm = makeVm({
    {"output-format", "mkv"             },
    {"input",         inputPath.string()}
  });
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
  auto const vm = makeVm({
    {"input", "missing.mp4"}
  });
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

  auto const vm = makeVm({
    {"input",  inputPath.string() },
    {"output", outputFile.string()}
  });
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

  auto const vm = makeVm({
    {"input",       inputPath.string() },
    {"ffmpeg-path", ffmpegFile.string()}
  });
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
    {
      {"input",         inputPath.string()},
      {"output",        outputDir.string()},
      {"type",          "picture"         },
      {"output-format", "webp"            }
  },
    {"yes", "recursive", "pack", "pack-only"}
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
  CHECK(config.outputPath == outputDir);
}
