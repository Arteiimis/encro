#include "infra/stop_signal.h"
#include "utils/utils.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#include "test_utils.h"

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <unistd.h>
#endif

namespace {

// Spawns exec2 on a worker thread, raises the stop request as soon as the
// child's flag file appears (its proof of having started), then joins.
// Replaces the old sleep-then-stop requesters, which raced child startup
// under parallel shard load. elapsed receives the exec2 duration for the
// caller's hang-guard assertion.
auto exec2StopOnFlag(
  std::string const& cmd,
  bool mergeStderr,
  fs::path const& flagPath,
  std::chrono::duration<double>& elapsed
) -> ExecResult {
  auto result = std::optional<ExecResult>{};
  auto child = std::jthread{[&] {
    auto const startedAt = std::chrono::steady_clock::now();
    result = exec2(cmd, mergeStderr);
    elapsed = std::chrono::steady_clock::now() - startedAt;
  }};
  auto const started =
    testutils::waitUntil([&] { return fs::exists(flagPath); }, std::chrono::seconds{10});
  stopsignal::requestStop();
  child.join();
  REQUIRE(started);
  REQUIRE(result.has_value());
  return std::move(*result);
}

}  // namespace

TEST_CASE("readUserIpt returns true when yesToAll", "[utils]") {
  CHECK(readUserIpt(true, ""));
}

TEST_CASE("readUserIpt renders the prompt as a plain question", "[utils]") {
  auto input = std::istringstream{"\n"};
  auto const cinGuard = testutils::ScopedCinBuf{input};

  auto accepted = false;
  auto const output =
    testutils::captureStdout([&] { accepted = readUserIpt(false, "Proceed? (Y/n): "); });

  // No leading badge or severity prefix: the question text and its choice
  // marker are the whole prompt.
  CHECK(accepted);
  CHECK(output == "Proceed? (Y/n): ");
}

TEST_CASE("readUserIpt reads input", "[utils]") {
  auto input = std::istringstream{"y\n"};
  auto const cinGuard = testutils::ScopedCinBuf{input};

  CHECK(readUserIpt(false, ""));
}

TEST_CASE("readUserIpt defaults to yes on empty input", "[utils]") {
  auto input = std::istringstream{"\n"};
  auto const cinGuard = testutils::ScopedCinBuf{input};

  CHECK(readUserIpt(false, ""));
}

TEST_CASE("readUserIpt returns false when input read is interrupted", "[utils]") {
  testutils::ScopedStopSignalReset stopGuard;

  auto input = std::istringstream{};
  auto const cinGuard = testutils::ScopedCinBuf{input};

  CHECK_FALSE(readUserIpt(false, ""));
}

TEST_CASE("readUserIpt ends interrupted prompt on a new line", "[utils]") {
  testutils::ScopedStopSignalReset stopGuard;

  auto input = std::istringstream{};
  auto const cinGuard = testutils::ScopedCinBuf{input};

  auto accepted = false;
  auto const output =
    testutils::captureStdout([&] { accepted = readUserIpt(false, "confirm? (Y/n): "); });

  CHECK_FALSE(accepted);
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

  TempDir temp;
  auto const flagPath = temp.path / "child-started";

#if defined(_WIN32)
  auto const cmd = std::string{
    "cmd /c \"type nul >" + flagPath.string() + " & ping -n 10 127.0.0.1 >nul\""
  };
#else
  auto const cmd = std::string{"sh -c ': > " + flagPath.string() + "; sleep 10'"};
#endif

  auto elapsed = std::chrono::duration<double>{};
  auto const result = exec2StopOnFlag(cmd, true, flagPath, elapsed);

  stopsignal::reset();

  CHECK(result.exitCode == stopsignal::kCanceledExitCode);
  // Hang guard: exec2 must return once the stop lands, not outlive it.
  CHECK(elapsed < 30s);
}

TEST_CASE("exec2 captures child stderr separately when merging is disabled", "[utils]") {
#if defined(_WIN32)
  auto const cmd = std::string{"cmd /c \"echo out-line & echo err-line 1>&2\""};
#else
  auto const cmd = std::string{"sh -c 'echo out-line; echo err-line 1>&2'"};
#endif

  auto const result = exec2(cmd, false);

  CHECK(result.exitCode == 0);
  // The merged output holds only stdout; the child's stderr lands in the
  // separate field, available to the caller and never forwarded anywhere.
  CHECK(result.output.find("out-line") != std::string::npos);
  CHECK(result.output.find("err-line") == std::string::npos);
  CHECK(result.stderrText.find("err-line") != std::string::npos);
}

TEST_CASE("exec2 keeps stderr in the merged output when merging is enabled", "[utils]") {
#if defined(_WIN32)
  auto const cmd = std::string{"cmd /c \"echo out-line & echo err-line 1>&2\""};
#else
  auto const cmd = std::string{"sh -c 'echo out-line; echo err-line 1>&2'"};
#endif

  auto const result = exec2(cmd);

  CHECK(result.exitCode == 0);
  CHECK(result.output.find("out-line") != std::string::npos);
  CHECK(result.output.find("err-line") != std::string::npos);
  CHECK(result.stderrText.empty());
}

TEST_CASE("exec2 separate stderr stays empty for silent children", "[utils]") {
#if defined(_WIN32)
  auto const cmd = std::string{"cmd /c \"echo out-line\""};
#else
  auto const cmd = std::string{"sh -c 'echo out-line'"};
#endif

  auto const result = exec2(cmd, false);

  CHECK(result.exitCode == 0);
  CHECK(result.output.find("out-line") != std::string::npos);
  CHECK(result.stderrText.empty());
}

TEST_CASE("extractFailureReason prefers the classifier-accepted stderr line", "[utils]") {
  auto const captured = std::string{"progress frame=1\n"};
  auto const stderrText = std::string{"\nUnable to open file: missing.mp4\nmore\n"};

  auto const reason =
    extractFailureReason(captured, stderrText, 1, [](std::string_view line) {
      return line.find("Unable") != std::string_view::npos;
    });

  CHECK(reason == "Unable to open file: missing.mp4");
}

TEST_CASE("extractFailureReason falls back to the last non-empty line", "[utils]") {
  auto const captured = std::string{"first line\nmid line\nlast line\n"};

  auto const reason = extractFailureReason(captured, "", 3, {});

  CHECK(reason == "last line");
}

TEST_CASE(
  "extractFailureReason falls back to the exit code when nothing carries a line",
  "[utils]"
) {
  CHECK(extractFailureReason("", "", 17, {}) == "exit code 17");
  CHECK(extractFailureReason("\n \n", "", 9, {}) == "exit code 9");
}

TEST_CASE("extractFailureReason caps long reasons", "[utils]") {
  auto const longLine = std::string(500, 'x');

  auto const reason = extractFailureReason(longLine, "", 1, {});

  CHECK(reason.size() <= 200);
}

TEST_CASE("exec2 reports a missing tool as exit 127, not a spawn exception", "[utils]") {
  // The real-ffmpeg tests' skip path (findFFmpeg -> exec2 of a bare name)
  // needs a non-zero exit code for a missing tool: the posix launcher used
  // to turn the execve ENOENT into a boost exception that aborted the suite.
  CHECK(exec2("definitely-not-a-real-command-xyz123").exitCode == 127);

#if !defined(_WIN32)
  CHECK(exec2("\"/definitely_missing_dir_xyz/tool\" -version").exitCode == 127);
#endif
}

TEST_CASE("exec2 resolves an unquoted tool path containing spaces", "[utils]") {
  // quoteToolPath emits bare paths on Windows, and the launcher historically
  // resolved them via the whitespace-extension search — the exit-127 token
  // check must not reject the first space-split token outright.
  namespace fs = std::filesystem;

#if defined(_WIN32)
  auto self = std::array<wchar_t, 1024>{};
  auto const len =
    GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
  REQUIRE(len > 0);
  REQUIRE(len < static_cast<DWORD>(self.size()));
  auto const exePath = fs::path{self.data()};
#else
  auto const exePath = fs::canonical("/proc/self/exe");
#endif

  TempDir temp;
  auto const spacedDir = temp.path / "tool tools dir";
  REQUIRE(fs::create_directory(spacedDir));
  auto const spacedExe = spacedDir / exePath.filename();
  REQUIRE(fs::copy_file(exePath, spacedExe));

  // Deliberately unquoted; --list-tests exits 0 without running the suite.
  auto const result = exec2(spacedExe.string() + " --list-tests");
  CHECK(result.exitCode == 0);
}

TEST_CASE(
  "exec2 closes the output pipe after stop even if another process keeps stdout open",
  "[utils]"
) {
  using namespace std::chrono_literals;

  stopsignal::reset();

  TempDir temp;
  auto const flagPath = temp.path / "child-started";

#if defined(_WIN32)
  // The start /b grandchild outlives the direct child and holds the stdout
  // pipe open; no PowerShell, whose cold start used to blow the old 2 s
  // elapsed bound under load.
  auto const cmd = std::string{
    "cmd /c \"type nul >"
    + flagPath.string()
    + " & start /b ping -n 3 127.0.0.1 & ping -n 5 127.0.0.1 >nul\""
  };
#else
  auto const cmd =
    std::string{"sh -c ': > " + flagPath.string() + "; sleep 3 & sleep 4'"};
#endif

  auto elapsed = std::chrono::duration<double>{};
  auto const result = exec2StopOnFlag(cmd, true, flagPath, elapsed);

  stopsignal::reset();

  CHECK(result.exitCode == stopsignal::kCanceledExitCode);
  // Hang guard: the surviving grandchild must not extend exec2's lifetime.
  CHECK(elapsed < 30s);
}

TEST_CASE("exec2 reports the child's exit code and pid", "[utils]") {
#if defined(_WIN32)
  auto const cmd = std::string{"cmd /c \"exit /b 7\""};
#else
  auto const cmd = std::string{"sh -c 'exit 7'"};
#endif

  auto const result = exec2(cmd);

  CHECK(result.exitCode == 7);
  CHECK(result.pid.has_value());
  CHECK(result.pid.value() > 0);
}

TEST_CASE(
  "exec2 merges stderr into stdout by default and captures it separately otherwise",
  "[utils]"
) {
#if defined(_WIN32)
  auto const cmd = std::string{"cmd /c \"echo out-line & echo err-line 1>&2\""};
#else
  auto const cmd = std::string{"sh -c 'echo out-line; echo err-line 1>&2'"};
#endif

  auto const merged = exec2(cmd);
  CHECK(merged.exitCode == 0);
  CHECK(merged.output.find("out-line") != std::string::npos);
  CHECK(merged.output.find("err-line") != std::string::npos);

  auto const separate = exec2(cmd, false);
  CHECK(separate.exitCode == 0);
  CHECK(separate.output.find("out-line") != std::string::npos);
  CHECK(separate.output.find("err-line") == std::string::npos);
}

TEST_CASE("exec2 captures output larger than one pipe buffer", "[utils]") {
#if defined(_WIN32)
  auto const cmd = std::string{"cmd /c \"for /l %i in (1,1,20000) do @echo line-%i\""};
#else
  auto const cmd = std::string{"sh -c 'seq 1 20000'"};
#endif

  auto const result = exec2(cmd);

  CHECK(result.exitCode == 0);
  CHECK(result.output.size() > 100000);
}

TEST_CASE("exec2 delivers one callback per line with CRLF stripped", "[utils]") {
#if defined(_WIN32)
  auto const cmd = std::string{"cmd /c \"echo alpha&echo beta\""};
#else
  auto const cmd = std::string{"sh -c 'printf \"alpha\\r\\nbeta\\r\\n\"'"};
#endif

  auto lines = std::vector<std::string>{};
  auto const result =
    exec2(cmd, [&](std::string_view line) { lines.emplace_back(line); });

  CHECK(result.exitCode == 0);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "alpha");
  CHECK(lines[1] == "beta");
}

TEST_CASE("exec2 keeps partial trailing output without a newline", "[utils]") {
#if defined(_WIN32)
  auto const cmd =
    std::string{"powershell -NoProfile -Command \"Write-Host -NoNewline abc\""};
#else
  auto const cmd = std::string{"sh -c 'printf abc'"};
#endif

  auto const result = exec2(cmd);

  CHECK(result.exitCode == 0);
  CHECK(result.output == "abc");
}

TEST_CASE("exec2 returns partial output captured before stop termination", "[utils]") {
  using namespace std::chrono_literals;

  auto resetGuard = testutils::ScopedStopSignalReset{};

  TempDir temp;
  auto const flagPath = temp.path / "child-started";

#if defined(_WIN32)
  auto const cmd = std::string{
    "cmd /c \"type nul >"
    + flagPath.string()
    + " & for /l %i in (1,1,100) do @(echo tick-%i & ping -n 1 "
      "127.0.0.1 >nul)\""
  };
#else
  auto const cmd = std::string{
    "sh -c ': > "
    + flagPath.string()
    + "; i=1; while [ $i -le 100 ]; do echo tick-$i; i=$((i+1)); sleep 0.1; done'"
  };
#endif

  auto elapsed = std::chrono::duration<double>{};
  auto const result = exec2StopOnFlag(cmd, true, flagPath, elapsed);

  CHECK(result.exitCode == stopsignal::kCanceledExitCode);
  CHECK_FALSE(result.output.empty());
  CHECK(result.output.find("tick-") != std::string::npos);
  // Hang guard only: at least one tick line is guaranteed by the flag poll.
  CHECK(elapsed < 30s);
}

TEST_CASE("exec2 cancels promptly when a stop is already requested", "[utils]") {
  testutils::ScopedStopSignalReset stopGuard;
  stopsignal::requestStop();

#if defined(_WIN32)
  auto const cmd = std::string{"ping -n 10 127.0.0.1"};
#else
  auto const cmd = std::string{"sleep 10"};
#endif

  auto const result = exec2(cmd);

  CHECK(result.exitCode == stopsignal::kCanceledExitCode);
}
