#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

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

TEST_CASE("test sources carry no unmarked synchronization sleeps", "[test-utils][meta]") {
  // Tests synchronize by polling observable state (see AGENTS.md Testing).
  // A sleep_for in test sources must carry a `// sleep-ok: <reason>` marker
  // on the same line; measurement and signal-cadence sleeps stay allowed
  // through the marker. The exempt files implement polling or the fake
  // media tool itself.
  auto const sourceDir = fs::path{ENCRO_TEST_SOURCE_DIR};
  REQUIRE(fs::exists(sourceDir));

  auto const exempt = std::array<fs::path, 2>{
    sourceDir / "e2e" / "fake_media_tool.cpp",
    sourceDir / "e2e" / "e2e_test_utils.cpp",
  };

  // The needle is assembled from pieces so this file's own source does not
  // contain the literal and trip the scan.
  auto const needle = std::string{"sleep_"} + "for";
  auto const marker = std::string_view{"sleep-ok"};

  auto offenders = std::vector<std::string>{};
  for (auto const& entry: fs::recursive_directory_iterator{sourceDir}) {
    if (!entry.is_regular_file() || entry.path().extension() != ".cpp") { continue; }
    if (std::ranges::find(exempt, entry.path()) != exempt.end()) { continue; }

    auto in = std::ifstream{entry.path()};
    REQUIRE(in.is_open());
    auto line = std::string{};
    auto lineNo = 0;
    while (std::getline(in, line)) {
      ++lineNo;
      if (line.find(needle) == std::string::npos) { continue; }
      if (line.find(marker) != std::string_view::npos) { continue; }
      offenders.push_back(
        std::format(
          "{}:{}: unmarked synchronization sleep — poll observable state, "
          "or add a `// {} <reason>` marker",
          entry.path().string(),
          lineNo,
          marker
        )
      );
    }
  }

  for (auto const& offender: offenders) { INFO(offender); }
  CHECK(offenders.empty());
}
