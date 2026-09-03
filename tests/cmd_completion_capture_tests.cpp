#include "cmd/cmd.h"
#include "cmd/completion_registry.h"

#include <CLI/CLI.hpp>

#include <catch2/catch_all.hpp>

#include <string>
#include <vector>

namespace {

// One shared build per process: registration populates the process-global
// registry, and the (leaked) app owns the options the names resolve against.
auto buildTree() -> CLI::App& {
  static auto& app = [] -> CLI::App& {
    auto result = CmdParseResult{};
    return *buildAppTree(result, "completion capture tests", false).app;
  }();
  return app;
}

}  // namespace

TEST_CASE("captured completion names resolve in the live tree", "[completion]") {
  auto& app = buildTree();
  // Root lookup only descends into nameless option groups; subcommand twins
  // (preview's --duration etc.) resolve in their own scope.
  auto const resolvable = [&app](std::string const& longName) {
    if (app.get_option_no_throw(longName) != nullptr) { return true; }
    for (auto* sub: app.get_subcommands([](CLI::App*) { return true; })) {
      if (sub->get_option_no_throw(longName) != nullptr) { return true; }
    }
    return false;
  };
  for (auto const& [longName, info]: completion::optionValues()) {
    INFO("option " << longName);
    REQUIRE(resolvable(longName));
  }
  for (auto const& [key, longName]: completion::configKeyOptions()) {
    INFO("config key " << key << " -> " << longName);
    REQUIRE(resolvable(longName));
  }
}

TEST_CASE("known options carry completion metadata", "[completion]") {
  auto& app = buildTree();
  (void)app;

  using Vec = std::vector<std::string>;

  REQUIRE(completion::valueInfoOf("--output-format")->candidates == Vec{"mp4", "webp"});
  REQUIRE(
    completion::valueInfoOf("--preset")->candidates
    == Vec{"auto", "p1", "p2", "p3", "p4", "p5", "p6", "p7"}
  );
  REQUIRE(
    completion::valueInfoOf("--color")->candidates == Vec{"auto", "always", "never"}
  );
  REQUIRE(completion::valueInfoOf("--type")->candidates == Vec{"video", "picture"});
  REQUIRE(
    completion::valueInfoOf("--force-conflict-handling")->candidates == Vec{"y", "n"}
  );

  for (auto const* numeric: {"--crf", "--jobs", "--image-quality", "--min-vmaf"}) {
    auto const* info = completion::valueInfoOf(numeric);
    REQUIRE(info != nullptr);
    REQUIRE(info->numeric);
  }

  // flags and free-text value options capture nothing
  REQUIRE(completion::valueInfoOf("--verbose") == nullptr);
  REQUIRE(completion::valueInfoOf("--video-codec") == nullptr);

  REQUIRE(*completion::longNameOfConfigKey("jobs") == "--jobs");
  REQUIRE(*completion::longNameOfConfigKey("output-format") == "--output-format");
}
