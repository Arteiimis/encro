#include "infra/crash_runtime.h"

#include "infra/stacktrace.h"
#include "logging/setup.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <format>
#include <fstream>
#include <string>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <csignal>
#endif

namespace {

auto gInstalled = std::atomic<bool>{false};

void writeToStderr(std::string const& message) {
  std::fwrite(message.data(), 1, message.size(), stderr);
  std::fflush(stderr);
}

// D-14/D-15: Direct file append bypassing spdlog entirely.
// Format manually to match spdlog kLogPattern: [timestamp] [critical] [infra.crash] message
// This is the first tier in the 3-tier fallback chain (D-16).
// All filesystem operations wrapped in try/catch — crash handler must never
// terminate from I/O failures (D-14, Pitfall #10).
auto tryWriteDirectToLogFile(std::string const& message) -> bool {
  try {
    auto const logPath = logging::currentLogFilePath();
    if (!logPath.has_value()) { return false; }

    auto ofs = std::ofstream(logPath.value(), std::ios::app);
    if (!ofs) { return false; }

    // Format timestamp to match spdlog kLogPattern (D-15)
    auto const now = std::chrono::system_clock::now();
    auto const t = std::chrono::system_clock::to_time_t(now);
    auto tm = std::tm{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    auto tsBuf = std::array<char, 64>{};
    std::strftime(tsBuf.data(), tsBuf.size(), "%Y-%m-%dT%H:%M:%S", &tm);

    auto const formatted =
      std::format("[{}] [critical] [infra.crash] {}\n", tsBuf.data(), message);

    ofs.write(formatted.data(), static_cast<std::streamsize>(formatted.size()));
    return true;
  } catch (...) { return false; }
}

auto tryWriteToLogger(std::string const& message) -> bool {
  auto* logger = spdlog::default_logger_raw();
  if (logger == nullptr) { return false; }

  try {
    logger->critical("{}", message);
    logger->flush();
    return true;
  } catch (...) { return false; }
}

void writeCrashMessage(std::string const& message) {
  // D-16: 3-tier fallback chain
  //   1) Direct file append — bypasses spdlog, survives async queue drain (D-14)
  //   2) tryWriteToLogger — existing spdlog path via default_logger_raw()
  //   3) writeToStderr — ultimate fallback, always available
  if (tryWriteDirectToLogFile(message)) { return; }
  if (tryWriteToLogger(message)) { return; }
  writeToStderr(message);
}

void writeCrashReport(std::string_view reason) {
  auto frames = crash::captureStacktrace(2);
  auto message = std::format(
    "\n[CRASH] {}\n[CRASH] stacktrace:\n{}",
    reason,
    crash::formatStacktrace(frames)
  );
  writeCrashMessage(message);
}

void terminateHandler() {
  try {
    auto exception = std::current_exception();
    if (exception != nullptr) { std::rethrow_exception(exception); }
  } catch (std::exception const& ex) {
    writeCrashReport(std::format("terminate after exception: {}", ex.what()));
    std::_Exit(1);
  } catch (...) {
    writeCrashReport("terminate after unknown exception");
    std::_Exit(1);
  }

  writeCrashReport("terminate without active exception");
  std::_Exit(1);
}

#if defined(_WIN32)
LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
  auto code = exceptionInfo == nullptr || exceptionInfo->ExceptionRecord == nullptr
    ? 0UL
    : exceptionInfo->ExceptionRecord->ExceptionCode;
  writeCrashReport(std::format("unhandled SEH exception code=0x{:08X}", code));
  return EXCEPTION_EXECUTE_HANDLER;
}
#else
void signalHandler(int signalNumber) {
  writeCrashReport(std::format("fatal signal {}", signalNumber));
  std::_Exit(128 + signalNumber);
}

void installSignalHandler(int signalNumber) {
  auto action = sigaction{};
  action.sa_handler = signalHandler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  sigaction(signalNumber, &action, nullptr);
}
#endif

}  // namespace

namespace crash {

void installHandlers() {
  auto expected = false;
  if (!gInstalled.compare_exchange_strong(expected, true)) { return; }

  std::set_terminate(terminateHandler);

#if defined(_WIN32)
  SetUnhandledExceptionFilter(unhandledExceptionFilter);
#else
  installSignalHandler(SIGABRT);
  installSignalHandler(SIGFPE);
  installSignalHandler(SIGILL);
  installSignalHandler(SIGSEGV);
#endif
}

void reportCaughtException(std::string_view context, std::exception const& exception) {
  writeCrashReport(std::format("{}: {}", context, exception.what()));
}

void reportUnknownException(std::string_view context) {
  writeCrashReport(std::format("{}: unknown exception", context));
}

}  // namespace crash
