#include "cmd/completion_registry.h"

#include <catch2/catch_all.hpp>

#include <string>
#include <vector>

namespace {

// The registries are process-global and shared with the capture tests' real
// build, so synthetic names must never leak past a test case: the guard wipes
// both maps even when an assertion throws.
struct RegistryGuard {
  RegistryGuard() { clear(); }
  ~RegistryGuard() { clear(); }
  static void clear() {
    completion::optionValues().clear();
    completion::configKeyOptions().clear();
  }
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
