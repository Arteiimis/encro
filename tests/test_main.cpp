#define CATCH_CONFIG_RUNNER

#include "infra/crash_runtime.h"

#include <catch2/catch_all.hpp>

#include <cstdlib>
#include <string_view>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <csignal>
#endif

namespace {

constexpr auto kCrashChildArg = std::string_view{"--encro-crash-child"};

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

auto main(int argc, char* argv[]) -> int {
  if (argc > 1 && std::string_view{argv[1]} == kCrashChildArg) { runCrashChild(); }
  // Unexpected crashes in any test produce a crash record (stack + reason) on
  // stderr instead of a bare exit code; idempotent, so the crash-child mode
  // (which installs its own) is unaffected.
  crash::installHandlers();
  return Catch::Session{}.run(argc, argv);
}
