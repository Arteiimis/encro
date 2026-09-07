#define CATCH_CONFIG_RUNNER

#include "infra/crash_runtime.h"

#include "infra/crash_runtime.h"
#include "infra/env.h"

#include <catch2/catch_all.hpp>                            // IWYU pragma: keep
#include <catch2/interfaces/catch_interfaces_capture.hpp>  // getResultCapture().getCurrentTestName()

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <vector>

#if defined(_WIN32)
  #include <windows.h>  // IWYU pragma: keep -- Windows-only (guarded by _WIN32)
#else
  #include <csignal>
#endif

namespace {

constexpr auto kCrashChildArg = std::string_view{"--encro-crash-child"};
constexpr auto kCrashChildOobArg = std::string_view{"--encro-crash-child=oob"};

// Isolates unit tests from any real user config: commandLineInit resolves the
// config file through ENCRO_CONFIG, so point it at a missing temp path by
// default. Individual tests override it with testutils::ScopedEnvVar.
void isolateConfigEnv() {
  auto const path =
    (std::filesystem::temp_directory_path() / "encro-unit-tests" / "missing-config.json")
      .string();
#if defined(_WIN32)
  ::_putenv_s("ENCRO_CONFIG", path.c_str());
#else
  ::setenv("ENCRO_CONFIG", path.c_str(), 1);
#endif
}

[[noreturn]] void runCrashChild() {
  crash::installHandlers();

#if defined(_WIN32)
  ::RaiseException(
    EXCEPTION_NONCONTINUABLE_EXCEPTION,
    EXCEPTION_NONCONTINUABLE,
    0,
    nullptr
  );
#else
  std::raise(SIGABRT);
#endif

  std::_Exit(1);
}

[[noreturn]] void runCrashChildOob() {
  crash::installHandlers();
  // Fixed name so the integration test can assert the context annotation made
  // it through the whole crash path.
  crash::setCrashContextProvider([]() -> std::string { return "crash-child-oob"; });
  // Hardened violation: traps under MSVC STL hardening (Windows, 0xC000001D ->
  // first-chance handler), asserts under _GLIBCXX_ASSERTIONS (Linux, abort ->
  // SIGABRT). Neither returns when hardening is on; _Exit is the fallback.
  std::vector<int> const victim{1, 2, 3};
  std::fprintf(stderr, "oob value: %d\n", victim[10]);
  std::_Exit(1);
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc > 1 && std::string_view{argv[1]} == kCrashChildArg) { runCrashChild(); }
  if (argc > 1 && std::string_view{argv[1]} == kCrashChildOobArg) { runCrashChildOob(); }
  // Unexpected crashes in any test produce a crash record (stack + reason) on
  // stderr instead of a bare exit code; idempotent, so the crash-child mode
  // (which installs its own) is unaffected.
  crash::installHandlers();
  // Crash records name the running test (parallel-shard logs locate the failure
  // without reproducing the crash). Outside a session (crash-child modes) this
  // throws; the crash path swallows provider failures by design.
  crash::setCrashContextProvider([]() -> std::string {
    return Catch::getResultCapture().getCurrentTestName();
  });
  isolateConfigEnv();
  return Catch::Session{}.run(argc, argv);
}

// Hidden test case: excluded from default runs, selected explicitly by the
// in-session integration test via the [crash-on-demand] spec filter plus the
// env gate. While Catch2's FatalConditionHandler owns the filter slot, the
// first-chance handler must still produce a crash record that names this test
// through the real provider installed in main.
TEST_CASE("in-session hardening crash", "[.][crash-on-demand]") {
  if (!processenv::readEnvVar("ENCRO_TEST_CRASH_OOB").has_value()) { return; }
  std::vector<int> const victim{1, 2, 3};
  std::fprintf(stderr, "oob value: %d\n", victim[10]);
}
