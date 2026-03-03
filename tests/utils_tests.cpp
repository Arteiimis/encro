#include "utils/utils.h"

#include <catch2/catch_all.hpp>

#include <iostream>
#include <sstream>
#include <string>

TEST_CASE("exec2 runs a simple command", "[utils]") {
  auto const result = exec2("echo hello");
  REQUIRE(result.exitCode == 0);
  CHECK(result.output.find("hello") != std::string::npos);
}

TEST_CASE("readUserIpt returns true when yesToAll", "[utils]") {
  CHECK(readUserIpt(true, ""));
}

TEST_CASE("readUserIpt reads input", "[utils]") {
  auto input = std::istringstream{"y\n"};
  auto* oldBuf = std::cin.rdbuf(input.rdbuf());

  auto const result = readUserIpt(false, "");

  std::cin.rdbuf(oldBuf);
  CHECK(result);
}
