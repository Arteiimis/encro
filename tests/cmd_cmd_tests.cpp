#include "cmd/cmd.h"

#include <catch2/catch_all.hpp>

#include <string>
#include <vector>

namespace {

auto parseArgs(std::vector<std::string> const& args) -> CmdParseResult {
  static thread_local std::vector<std::string> storage;
  storage = args;
  auto argv = std::vector<char*>{};
  argv.reserve(storage.size());
  for (auto& arg: storage) { argv.push_back(arg.data()); }
  argv.push_back(nullptr);

  return commandLineInit(static_cast<int>(argv.size() - 1), argv.data());
}

}  // namespace

TEST_CASE("commandLineInit exposes defaults", "[cmd]") {
  auto const result = parseArgs({"encro"});

  REQUIRE(result.vm.count("type") == 1);
  REQUIRE(result.vm.count("output-format") == 1);
  CHECK(result.vm["type"].as<std::string>() == "video");
  CHECK(result.vm["output-format"].as<std::string>() == "mp4");
}

TEST_CASE("commandLineInit parses flags", "[cmd]") {
  auto const result = parseArgs(
    {"encro", "--yes", "--recursive", "--pack", "--pack-only", "--verbose"}
  );

  CHECK(result.vm.count("yes") == 1);
  CHECK(result.vm.count("recursive") == 1);
  CHECK(result.vm.count("pack") == 1);
  CHECK(result.vm.count("pack-only") == 1);
  CHECK(result.vm.count("verbose") == 1);
}

TEST_CASE("commandLineInit parses multi-input values", "[cmd]") {
  auto const result = parseArgs({"encro", "-I", "a.mp4", "b.mkv", "c.mov"});

  REQUIRE(result.vm.count("inputs") == 1);
  auto const inputs = result.vm["inputs"].as<std::vector<std::string>>();
  CHECK(inputs.size() == 3);
  CHECK(inputs[0] == "a.mp4");
  CHECK(inputs[1] == "b.mkv");
  CHECK(inputs[2] == "c.mov");
}
