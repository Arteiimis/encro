// naming_plan_tests.cpp — Unit tests for the shared grouped-candidate naming
// planner behind the video output planner and the picture zip-entry planner.
//
// The planner owns the two rules neither flow may re-derive: which branch a
// group takes, and the order colliding inputs are named in.

#include "core/naming_plan.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Records which callback ran, for which input, in call order.
struct NameCalls {
  std::vector<std::string> order;

  auto unique() {
    return [this](fs::path const& input, fs::path const&) -> std::string {
      order.push_back(std::format("unique:{}", input.generic_string()));
      return "unique";
    };
  }

  auto conflict() {
    return [this](fs::path const& input, fs::path const&) -> std::string {
      order.push_back(std::format("conflict:{}", input.generic_string()));
      return "conflict";
    };
  }
};

// Two inputs collide when their last component matches, which is what both
// flows group on.
auto byFileName(fs::path const& input) -> fs::path {
  return input.filename();
}

}  // namespace

TEST_CASE(
  "planNamesByCandidate names each group by its size and force flag",
  "[naming]"
) {
  SECTION("a lone input takes the unique name") {
    auto calls = NameCalls{};
    auto const inputs = std::vector<fs::path>{fs::path{"sub"} / "a.png"};

    auto const planned = core::planNamesByCandidate<
      std::string
    >(inputs, byFileName, false, calls.unique(), calls.conflict());

    CHECK(calls.order == std::vector<std::string>{"unique:sub/a.png"});
    CHECK(planned.at(fs::path{"sub"} / "a.png") == "unique");
  }

  SECTION("forcing conflict naming overrides the lone-input shortcut") {
    auto calls = NameCalls{};
    auto const inputs = std::vector<fs::path>{fs::path{"sub"} / "a.png"};

    auto const planned = core::planNamesByCandidate<
      std::string
    >(inputs, byFileName, true, calls.unique(), calls.conflict());

    CHECK(calls.order == std::vector<std::string>{"conflict:sub/a.png"});
    CHECK(planned.at(fs::path{"sub"} / "a.png") == "conflict");
  }

  SECTION("colliding inputs are named in case-folded path order") {
    // A raw comparison puts "Zoo/a.png" first ('Z' < 'a'); stablePathString
    // folds case, so "abc" sorts first. Flows depend on that order, so the
    // planner states it once here.
    auto calls = NameCalls{};
    auto const inputs =
      std::vector<fs::path>{fs::path{"Zoo"} / "a.png", fs::path{"abc"} / "a.png"};

    auto const planned = core::planNamesByCandidate<
      std::string
    >(inputs, byFileName, false, calls.unique(), calls.conflict());

    CHECK(
      calls.order == std::vector<std::string>{"conflict:abc/a.png", "conflict:Zoo/a.png"}
    );
    CHECK(planned.size() == 2);
    CHECK(planned.at(fs::path{"abc"} / "a.png") == "conflict");
    CHECK(planned.at(fs::path{"Zoo"} / "a.png") == "conflict");
  }
}
