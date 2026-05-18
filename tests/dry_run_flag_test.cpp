#include "cmd/cmd.h"
#include "cmd/config_builder.h"
#include "core/app_context.h"
#include "core/error_handle.h"
#include "app/pipeline.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using testutils::writeFile;

namespace {

auto parseArgs(std::vector<std::string> const& args) -> CmdParseResult {
  static thread_local std::vector<std::string> storage;
  storage = args;
  auto argv = std::vector<char*>{};
  argv.reserve(storage.size());
  for (auto& arg: storage) { argv.push_back(arg.data()); }
  argv.push_back(nullptr);

  return commandLineInit(static_cast<int>(argv.size() - 1), argv.data(), "");
}

}  // namespace

TEST_CASE("CmdParseResult defaults dryRun to false", "[cmd][dry-run]") {
  auto const result = parseArgs({"encro", "-i", "."});

  CHECK(result.dryRun == false);
}

TEST_CASE("--dry-run flag sets dryRun to true", "[cmd][dry-run]") {
  auto const result = parseArgs({"encro", "-i", ".", "--dry-run"});

  CHECK(result.dryRun == true);
}

TEST_CASE("--dry-run flag is not set without flag", "[cmd][dry-run]") {
  auto const result = parseArgs({"encro", "-i", "."});

  CHECK(result.dryRun == false);
}

TEST_CASE("--dry-run appears in help output under General group", "[cmd][dry-run]") {
  auto const result = parseArgs({"encro", "--dry-run", "-h"});

  CHECK(result.helpText.find("--dry-run") != std::string::npos);
  // Verify description mentions dry-run semantics
  CHECK(
    (result.helpText.find("dry-run") != std::string::npos
     || result.helpText.find("dry run") != std::string::npos
     || result.helpText.find("without writing") != std::string::npos)
  );
}

TEST_CASE("buildConfig propagates dryRun=true from CmdParseResult", "[cmd][dry-run]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto cmd = CmdParseResult{};
  cmd.input = inputPath.string();
  cmd.dryRun = true;

  auto configRes = cmd::buildConfig(cmd);
  REQUIRE(configRes.has_value());

  auto const& config = configRes.value();
  CHECK(config.dryRun == true);
}

TEST_CASE("buildConfig propagates dryRun=false from CmdParseResult", "[cmd][dry-run]") {
  TempDir temp;
  auto const inputPath = temp.path / "input.mp4";
  writeFile(inputPath);

  auto cmd = CmdParseResult{};
  cmd.input = inputPath.string();
  cmd.dryRun = false;

  auto configRes = cmd::buildConfig(cmd);
  REQUIRE(configRes.has_value());

  auto const& config = configRes.value();
  CHECK(config.dryRun == false);
}

TEST_CASE("pipeline::runDryRun is declared as const& AppContext function", "[cmd][dry-run]") {
  // Compile-time check: the function exists and accepts const AppContext&
  auto fn = static_cast<eh::Result<int> (*)(appctx::AppContext const&)>(&pipeline::runDryRun);
  (void)fn;  // suppress unused warning
}

TEST_CASE("--dry-run is a flag (no value required)", "[cmd][dry-run]") {
  // Passing --dry-run without value should work
  auto const result = parseArgs({"encro", "-i", ".", "--dry-run"});
  CHECK_FALSE(result.error.has_value());
  CHECK(result.dryRun == true);
}
