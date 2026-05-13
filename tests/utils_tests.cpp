#include "infra/stop_signal.h"
#include "utils/utils.h"

#include <catch2/catch_all.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <thread>

#include "test_utils.h"

#if defined(_WIN32)
  #include <io.h>
#else
  #include <unistd.h>
#endif

namespace {

template<class Fn>
auto captureStdout(Fn&& action) -> std::string {
  std::fflush(stdout);

#if defined(_WIN32)
  auto* tempFile = static_cast<FILE*>(nullptr);
  REQUIRE(tmpfile_s(&tempFile) == 0);
#else
  auto* tempFile = std::tmpfile();
#endif
  REQUIRE(tempFile != nullptr);

#if defined(_WIN32)
  auto const stdoutFd = _fileno(stdout);
  auto const originalFd = _dup(stdoutFd);
  REQUIRE(originalFd >= 0);
  REQUIRE(_dup2(_fileno(tempFile), stdoutFd) == 0);
#else
  auto const stdoutFd = fileno(stdout);
  auto const originalFd = dup(stdoutFd);
  REQUIRE(originalFd >= 0);
  REQUIRE(dup2(fileno(tempFile), stdoutFd) == 0);
#endif

  action();

  std::fflush(stdout);
  std::rewind(tempFile);

  auto output = std::string{};
  auto buffer = std::array<char, 256>{};
  for (;;) {
    auto const bytesRead = std::fread(buffer.data(), 1, buffer.size(), tempFile);
    if (bytesRead == 0) { break; }
    output.append(buffer.data(), bytesRead);
  }

#if defined(_WIN32)
  REQUIRE(_dup2(originalFd, stdoutFd) == 0);
  _close(originalFd);
#else
  REQUIRE(dup2(originalFd, stdoutFd) == 0);
  close(originalFd);
#endif
  std::fclose(tempFile);

  return output;
}

}  // namespace

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

TEST_CASE("readUserIpt defaults to yes on empty input", "[utils]") {
  auto input = std::istringstream{"\n"};
  auto* oldBuf = std::cin.rdbuf(input.rdbuf());

  auto const result = readUserIpt(false, "");

  std::cin.rdbuf(oldBuf);
  CHECK(result);
}

TEST_CASE("readUserIpt returns false when input read is interrupted", "[utils]") {
  testutils::ScopedStopSignalReset stopGuard;

  auto input = std::istringstream{};
  auto* oldBuf = std::cin.rdbuf(input.rdbuf());

  auto const result = readUserIpt(false, "");

  std::cin.rdbuf(oldBuf);
  std::cin.clear();
  CHECK_FALSE(result);
}

TEST_CASE("readUserIpt ends interrupted prompt on a new line", "[utils]") {
  testutils::ScopedStopSignalReset stopGuard;

  auto input = std::istringstream{};
  auto* oldBuf = std::cin.rdbuf(input.rdbuf());

  auto const output = captureStdout([&] {
    auto const result = readUserIpt(false, "confirm? (Y/n): ");
    CHECK_FALSE(result);
  });

  std::cin.rdbuf(oldBuf);
  std::cin.clear();

  CHECK(output == "confirm? (Y/n): \n");
}

TEST_CASE("readUserIpt treats stop request as cancel even with yesToAll", "[utils]") {
  testutils::ScopedStopSignalReset stopGuard;
  stopsignal::requestStop();

  CHECK_FALSE(readUserIpt(true, ""));
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
