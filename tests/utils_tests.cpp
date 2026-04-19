#include "infra/stop_signal.h"
#include "utils/utils.h"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

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

TEST_CASE("exec2 terminates child process when stop is requested", "[utils]") {
  using namespace std::chrono_literals;

  stopsignal::reset();

#if defined(_WIN32)
  auto const cmd = std::string{"ping -n 10 127.0.0.1"};
#else
  auto const cmd = std::string{"sleep 10"};
#endif

  auto requester = std::jthread([](std::stop_token token) {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(150ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const startedAt = std::chrono::steady_clock::now();
  auto const result = exec2(cmd, true);
  auto const elapsed = std::chrono::steady_clock::now() - startedAt;

  stopsignal::reset();

  CHECK(result.exitCode == stopsignal::kCanceledExitCode);
  CHECK(elapsed < 5s);
}

TEST_CASE(
  "exec2 closes the output pipe after stop even if another process keeps stdout open",
  "[utils]"
) {
  using namespace std::chrono_literals;

  stopsignal::reset();

#if defined(_WIN32)
  auto const cmd = std::string{
    "cmd /c \"start /b powershell -NoProfile -Command Start-Sleep -Seconds 3 & "
    "ping -n 5 127.0.0.1 >nul\""
  };
#else
  auto const cmd = std::string{"sh -c 'sleep 3 & sleep 4'"};
#endif

  auto requester = std::jthread([](std::stop_token token) {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(150ms);
    if (!token.stop_requested()) { stopsignal::requestStop(); }
  });

  auto const startedAt = std::chrono::steady_clock::now();
  auto const result = exec2(cmd, true);
  auto const elapsed = std::chrono::steady_clock::now() - startedAt;

  stopsignal::reset();

  CHECK(result.exitCode == stopsignal::kCanceledExitCode);
  CHECK(elapsed < 2s);
}
