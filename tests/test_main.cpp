#define CATCH_CONFIG_RUNNER

#include "infra/crash_runtime.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <cstdlib>
#include <filesystem>
#include <string_view>

#if defined(_WIN32)
  #include <windows.h>  // IWYU pragma: keep -- Windows-only (guarded by _WIN32)
#else
  #include <csignal>
#endif

namespace {

constexpr auto kCrashChildArg = std::string_view{"--encro-crash-child"};

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

}  // namespace

int main(int argc, char* argv[]) {
  if (argc > 1 && std::string_view{argv[1]} == kCrashChildArg) { runCrashChild(); }
  // Unexpected crashes in any test produce a crash record (stack + reason) on
  // stderr instead of a bare exit code; idempotent, so the crash-child mode
  // (which installs its own) is unaffected.
  crash::installHandlers();
  isolateConfigEnv();
  return Catch::Session{}.run(argc, argv);
}
