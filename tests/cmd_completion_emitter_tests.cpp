#include "cmd/completion_emitter.h"

#include "cmd/cmd.h"
#include "cmd/completion_registry.h"
#include "test_utils.h"

#include <CLI/CLI.hpp>

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

auto findOption(completion::ScopeInfo const& scope, std::string const& id)
  -> completion::OptionInfo const* {
  for (auto const& option: scope.options) {
    if (option.id == id) { return &option; }
  }
  return nullptr;
}

auto findScope(completion::CompletionModel const& model, std::string const& name)
  -> completion::ScopeInfo const* {
  for (auto const& scope: model.subcommands) {
    if (scope.name == name) { return &scope; }
  }
  return nullptr;
}

auto has(std::vector<std::string> const& values, std::string const& needle) -> bool {
  return std::find(values.begin(), values.end(), needle) != values.end();
}

auto contains(std::string const& haystack, std::string const& needle) -> bool {
  return haystack.find(needle) != std::string::npos;
}

// Every registered option name in the live tree (all scopes).
auto allTreeNames() -> std::vector<std::string> {
  auto result = CmdParseResult{};
  auto tree = buildAppTree(result, "emitter tests", false);
  auto names = std::vector<std::string>{};
  auto const collect = [&names](CLI::App const& app) {
    for (auto* option: app.get_options()) {
      for (auto const& lname: option->get_lnames()) { names.push_back("--" + lname); }
      for (auto const& sname: option->get_snames()) { names.push_back("-" + sname); }
    }
  };
  collect(*tree.app);
  for (auto* sub: tree.app->get_subcommands([](CLI::App*) { return true; })) {
    collect(*sub);
  }
  return names;
}

}  // namespace

TEST_CASE("model walks scopes with value metadata", "[completion]") {
  auto const model = completion::buildCompletionModel();

  auto const* recursive = findOption(model.main, "recursive");
  REQUIRE(recursive != nullptr);
  REQUIRE(
    recursive->names == std::vector<std::string>{"--no-recursive", "--recursive", "-r"}
  );
  REQUIRE_FALSE(recursive->takesValue);

  auto const* outputFormat = findOption(model.main, "output_format");
  REQUIRE(outputFormat != nullptr);
  REQUIRE(outputFormat->takesValue);
  REQUIRE(outputFormat->candidates == std::vector<std::string>{"mp4", "webp"});

  auto const* crf = findOption(model.main, "crf");
  REQUIRE(crf != nullptr);
  REQUIRE(crf->numeric);
  REQUIRE(crf->candidates.empty());

  // free-text value option and flag carry no candidates
  REQUIRE(findOption(model.main, "video_codec")->candidates.empty());
  REQUIRE(findOption(model.main, "video_codec")->numeric == false);
  REQUIRE(findOption(model.main, "verbose")->candidates.empty());
}

TEST_CASE("model keeps subcommand scopes separate", "[completion]") {
  auto const model = completion::buildCompletionModel();

  REQUIRE(findScope(model, "preview") != nullptr);
  REQUIRE(findScope(model, "config") != nullptr);

  auto const* preview = findScope(model, "preview");
  REQUIRE(findOption(*preview, "start") != nullptr);
  REQUIRE(findOption(*preview, "duration") != nullptr);
  REQUIRE(findOption(*preview, "pack") == nullptr);  // main-only

  auto const* config = findScope(model, "config");
  REQUIRE(findOption(*config, "set") != nullptr);
  REQUIRE(findOption(*config, "jobs") == nullptr);  // main-only
}

TEST_CASE(
  "model exclusion graph is symmetric across one-sided declarations",
  "[completion]"
) {
  auto const model = completion::buildCompletionModel();

  auto const* resume = findOption(model.main, "resume");
  auto const* restart = findOption(model.main, "restart");
  REQUIRE(resume != nullptr);
  REQUIRE(restart != nullptr);
  REQUIRE(has(resume->hiddenBy, "--restart"));
  REQUIRE(has(restart->hiddenBy, "--resume"));

  auto const* inputs = findOption(model.main, "inputs");
  auto const* input = findOption(model.main, "input");
  REQUIRE(inputs != nullptr);
  REQUIRE(input != nullptr);
  REQUIRE(has(inputs->hiddenBy, "--input"));
  REQUIRE(has(inputs->hiddenBy, "-i"));
  REQUIRE(has(input->hiddenBy, "--inputs"));
  REQUIRE(has(input->hiddenBy, "-I"));

  auto const* setAction = findOption(*findScope(model, "config"), "set");
  auto const* getAction = findOption(*findScope(model, "config"), "get");
  REQUIRE(setAction != nullptr);
  REQUIRE(getAction != nullptr);
  for (auto const* other: {"--list", "--get", "--unset", "--path"}) {
    REQUIRE(has(setAction->hiddenBy, other));
  }
  REQUIRE(has(getAction->hiddenBy, "--set"));
}

TEST_CASE("model path ids resolve to real value options", "[completion]") {
  auto const model = completion::buildCompletionModel();
  REQUIRE_FALSE(model.pathIds.empty());
  for (auto const& id: model.pathIds) {
    INFO("path id " << id);
    auto found = findOption(model.main, id) != nullptr;
    for (auto const& scope: model.subcommands) {
      found = found || findOption(scope, id) != nullptr;
    }
    REQUIRE(found);
  }
}

TEST_CASE("model collects config keys with enumerated values", "[completion]") {
  auto const model = completion::buildCompletionModel();
  REQUIRE(has(model.configKeys, "jobs"));
  REQUIRE(has(model.configKeys, "output-format"));
  REQUIRE(
    model.configKeyValues.at("output-format") == std::vector<std::string>{"mp4", "webp"}
  );
}

TEST_CASE("emitted scripts cover the full surface deterministically", "[completion]") {
  auto const model = completion::buildCompletionModel();
  auto const bash = completion::emitBashScript(model);
  auto const powershell = completion::emitPowerShellScript(model);

  for (auto const* script: {&bash, &powershell}) {
    for (auto const& name: allTreeNames()) {
      INFO("name " << name);
      REQUIRE(contains(*script, name));
      REQUIRE_FALSE(contains(*script, "{false}"));
    }
  }

  REQUIRE(bash == completion::emitBashScript(model));
  REQUIRE(powershell == completion::emitPowerShellScript(model));

  REQUIRE(contains(bash, "_ENCRO_CANDS_output_format=\"mp4 webp\""));
  REQUIRE(contains(bash, "_ENCRO_HIDDEN_resume=\"--restart\""));
  REQUIRE(contains(bash, "_ENCRO_HIDDEN_restart=\"--resume\""));
  REQUIRE(contains(bash, "compgen -W \"$_ENCRO_CONFIG_KEYS\""));
  REQUIRE(contains(powershell, "'output_format' = @('mp4', 'webp')"));
  REQUIRE(contains(powershell, "Register-ArgumentCompleter -CommandName encro -Native"));
}

TEST_CASE("emission is independent of stored config", "[completion]") {
  auto const model = completion::buildCompletionModel();
  auto const baseline = completion::emitBashScript(model);

  TempDir temp;
  auto const configPath = (temp.path / "broken.json").string();
  auto const withEnv = "ENCRO_CONFIG=" + configPath;
  _putenv(withEnv.c_str());

  auto const underEnv = completion::emitBashScript(completion::buildCompletionModel());
  _putenv("ENCRO_CONFIG=");

  REQUIRE(underEnv == baseline);
}
