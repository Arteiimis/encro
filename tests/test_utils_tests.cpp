#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("TempDir keeps its directory when destroyed during unwinding", "[test-utils]") {
  TempDir outer;
  auto const errFile = outer.path / "stderr.txt";
  auto keptPath = fs::path{};

  {
    auto capture = testutils::StderrCapture{errFile};
    try {
      TempDir temp;
      keptPath = temp.path;
      throw std::runtime_error{"boom"};
    } catch (...) { }
  }

  auto const errText = testutils::readTextFile(errFile);
  CHECK(fs::exists(keptPath));
  CHECK(errText.find("kept temp dir on failure") != std::string::npos);
  CHECK(errText.find(keptPath.string()) != std::string::npos);

  std::error_code ec;
  fs::remove_all(keptPath, ec);
}

TEST_CASE("TempDir removes its directory on normal scope exit", "[test-utils]") {
  auto path = fs::path{};
  {
    TempDir temp;
    path = temp.path;
    REQUIRE(fs::exists(path));
  }
  CHECK(!fs::exists(path));
}

TEST_CASE("waitUntil returns as soon as the predicate holds", "[test-utils]") {
  auto const startedAt = std::chrono::steady_clock::now();
  auto calls = 0;
  auto const held = testutils::waitUntil(
    [&calls] { return ++calls >= 3; },
    std::chrono::seconds{5},
    std::chrono::milliseconds{1}
  );
  auto const elapsed = std::chrono::steady_clock::now() - startedAt;

  REQUIRE(held);
  CHECK(calls == 3);
  // Returned before the timeout expired (hang guard, not a tight bound).
  CHECK(elapsed < std::chrono::seconds{5});
}

TEST_CASE("waitUntil reports timeout when the predicate never holds", "[test-utils]") {
  auto const startedAt = std::chrono::steady_clock::now();
  auto const held = testutils::waitUntil(
    [] { return false; },
    std::chrono::milliseconds{50},
    std::chrono::milliseconds{5}
  );
  auto const elapsed = std::chrono::steady_clock::now() - startedAt;

  // Callers REQUIRE this result naming the awaited condition: no silent pass.
  CHECK_FALSE(held);
  CHECK(elapsed >= std::chrono::milliseconds{40});
  CHECK(elapsed < std::chrono::seconds{5});
}

TEST_CASE("waitUntil consults the predicate once past the deadline", "[test-utils]") {
  // Zero timeout: the poll loop never runs; the trailing re-check decides,
  // so a condition satisfied between polls is not reported as a timeout.
  CHECK(testutils::waitUntil([] { return true; }, std::chrono::milliseconds{0}));
  CHECK_FALSE(testutils::waitUntil([] { return false; }, std::chrono::milliseconds{0}));
}
