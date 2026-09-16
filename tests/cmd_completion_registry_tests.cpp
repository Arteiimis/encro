#include "cmd/completion_registry.h"

#include <CLI/CLI.hpp>

#include <catch2/catch_all.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

// The registries are process-global and shared with the capture tests' real
// build, so synthetic names must never leak past a test case -- and the real
// entries must survive one: snapshot them, clear for the test, restore after.
struct RegistryGuard {
  RegistryGuard()
    : options_(completion::optionValues()),
      config_(completion::configKeyOptions()),
      paths_(completion::pathOptions()),
      positionals_(completion::positionalOptions()) {
    clear();
  }

  ~RegistryGuard() {
    completion::optionValues() = std::move(options_);
    completion::configKeyOptions() = std::move(config_);
    completion::pathOptions() = std::move(paths_);
    completion::positionalOptions() = std::move(positionals_);
  }

  RegistryGuard(RegistryGuard const&) = delete;
  auto operator=(RegistryGuard const&) -> RegistryGuard& = delete;

private:
  static void clear() {
    completion::optionValues().clear();
    completion::configKeyOptions().clear();
    completion::pathOptions().clear();
    completion::positionalOptions().clear();
  }

  std::map<std::string, completion::ValueInfo> options_;
  std::map<std::string, std::string> config_;
  std::set<std::string> paths_;
  std::map<CLI::Option const*, std::vector<std::string>> positionals_;
};

}  // namespace

TEST_CASE("registry records enumerated candidates", "[completion]") {
  RegistryGuard guard;
  completion::recordCandidates("--output-format", {"mp4", "webp"});
  auto const* info = completion::valueInfoOf("--output-format");
  REQUIRE(info != nullptr);
  REQUIRE_FALSE(info->numeric);
  REQUIRE(info->candidates == std::vector<std::string>{"mp4", "webp"});
  REQUIRE(completion::valueInfoOf("--unknown") == nullptr);
}

TEST_CASE("registry records numeric options", "[completion]") {
  RegistryGuard guard;
  completion::recordNumeric("--crf");
  auto const* info = completion::valueInfoOf("--crf");
  REQUIRE(info != nullptr);
  REQUIRE(info->numeric);
  REQUIRE(info->candidates.empty());
}

TEST_CASE("registry keeps both markers regardless of capture order", "[completion]") {
  RegistryGuard guard;
  completion::recordNumeric("--numeric-first");
  completion::recordCandidates("--numeric-first", {"a"});

  completion::recordCandidates("--candidates-first", {"a", "b"});
  completion::recordNumeric("--candidates-first");

  for (auto const* name: {"--numeric-first", "--candidates-first"}) {
    auto const* info = completion::valueInfoOf(name);
    REQUIRE(info != nullptr);
    REQUIRE(info->numeric);
    REQUIRE_FALSE(info->candidates.empty());
  }
}

TEST_CASE("registry re-record overwrites candidates", "[completion]") {
  RegistryGuard guard;
  completion::recordCandidates("--opt", {"old"});
  completion::recordCandidates("--opt", {"new"});
  REQUIRE(
    completion::valueInfoOf("--opt")->candidates == std::vector<std::string>{"new"}
  );
}

TEST_CASE("registry maps config keys to long names", "[completion]") {
  RegistryGuard guard;
  completion::recordConfigKey("jobs", "--jobs");
  completion::recordConfigKey("crf", "--crf");
  completion::recordConfigKey("jobs", "--jobs");

  REQUIRE(completion::configKeys() == std::vector<std::string>{"crf", "jobs"});
  REQUIRE(*completion::longNameOfConfigKey("jobs") == "--jobs");
  REQUIRE(*completion::longNameOfConfigKey("crf") == "--crf");
  REQUIRE(completion::longNameOfConfigKey("nope") == nullptr);

  completion::recordConfigKey("jobs", "--renamed");
  REQUIRE(*completion::longNameOfConfigKey("jobs") == "--renamed");
}

TEST_CASE("registry records path options by long name", "[completion]") {
  RegistryGuard guard;
  completion::recordPath("--model-dir");
  completion::recordPath("--input");
  completion::recordPath("--model-dir");  // set semantics: re-record is a no-op

  REQUIRE(completion::pathOptions() == std::set<std::string>{"--input", "--model-dir"});
}

TEST_CASE("registry records positional candidates by option pointer", "[completion]") {
  RegistryGuard guard;
  CLI::App app{"registry tests"};
  auto* shell = app.add_option("shell", "target shell");
  auto* dir = app.add_option("dir", "folder");

  completion::recordPositional(shell, {"bash", "powershell"});

  REQUIRE(
    *completion::positionalCandidatesOf(shell)
    == std::vector<std::string>{"bash", "powershell"}
  );
  REQUIRE(completion::positionalCandidatesOf(dir) == nullptr);
}

TEST_CASE("registry guard restores the metadata it hides", "[completion]") {
  // Every map the guard borrows has to come back: the capture tests read the
  // option and config-key registries without building the tree themselves.
  RegistryGuard outer;
  completion::recordCandidates("--output-format", {"mp4", "webp"});
  completion::recordConfigKey("jobs", "--jobs");
  completion::recordPath("--model-dir");
  CLI::App probe{"registry probe"};
  auto* shell = probe.add_option("shell", "target shell");
  completion::recordPositional(shell, {"bash"});

  {
    RegistryGuard inner;
    REQUIRE(completion::valueInfoOf("--output-format") == nullptr);
    REQUIRE(completion::longNameOfConfigKey("jobs") == nullptr);
    REQUIRE(completion::pathOptions().empty());
    REQUIRE(completion::positionalCandidatesOf(shell) == nullptr);
  }

  auto const* info = completion::valueInfoOf("--output-format");
  REQUIRE(info != nullptr);
  REQUIRE(info->candidates == std::vector<std::string>{"mp4", "webp"});
  auto const* longName = completion::longNameOfConfigKey("jobs");
  REQUIRE(longName != nullptr);
  REQUIRE(*longName == "--jobs");
  REQUIRE(completion::pathOptions() == std::set<std::string>{"--model-dir"});
  auto const* positional = completion::positionalCandidatesOf(shell);
  REQUIRE(positional != nullptr);
  REQUIRE(*positional == std::vector<std::string>{"bash"});
}
