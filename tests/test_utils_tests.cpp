#include "test_utils.h"

#include <catch2/catch_test_macros.hpp>

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
