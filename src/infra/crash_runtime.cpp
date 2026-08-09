#include "infra/crash_runtime.h"

#include "infra/stacktrace.h"
#include "logging/logging.h"
#include "logging/setup.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
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
// Timestamp carries millisecond + timezone-offset precision so crash lines sort
// correctly next to regular lines from the same second (D-15).
// This is the first tier in the 3-tier fallback chain (D-16).
// All filesystem operations wrapped in try/catch — crash handler must never
// terminate from I/O failures (D-14, Pitfall #10).
// Bounded retry covers the µs-scale rotating-sink close→reopen window where a
// momentary open failure would otherwise fall through to tier 2.
auto tryWriteDirectToLogFile(std::string const& message) -> bool {
  constexpr auto kMaxAttempts = 3;
  constexpr auto kRetryDelay = std::chrono::milliseconds{10};

  for (auto attempt = 0; attempt < kMaxAttempts; ++attempt) {
    try {
      auto const logPath = logging::currentLogFilePath();
      if (!logPath.has_value()) { return false; }
      // Lock-free snapshot: the crashing thread may hold the run-id mutex
      auto const runId = logging::runIdSnapshot();

      auto ofs = std::ofstream(logPath.value(), std::ios::app);
      if (!ofs) {
        if (attempt + 1 < kMaxAttempts) {
          std::this_thread::sleep_for(kRetryDelay);
          continue;
        }
        return false;
      }

      // Format timestamp to match spdlog kLogPattern (D-15)
      auto const now = std::chrono::system_clock::now();
      auto const t = std::chrono::system_clock::to_time_t(now);
      auto const ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
          .count()
        % 1000
      );
      auto tm = std::tm{};
#if defined(_WIN32) || defined(_WIN64)
      localtime_s(&tm, &t);
#else
      localtime_r(&t, &tm);
#endif
      auto tsBuf = std::array<char, 64>{};
      std::strftime(tsBuf.data(), tsBuf.size(), "%Y-%m-%dT%H:%M:%S", &tm);
      auto offBuf = std::array<char, 16>{};
      std::strftime(offBuf.data(), offBuf.size(), "%z", &tm);
      // spdlog's %z renders ISO 8601 +HH:MM (with colon); MSVC strftime
      // emits +HHMM — normalize so crash lines sort with regular lines.
      auto const offRaw = std::string_view{offBuf.data()};
      auto tzOffset = std::string{};
      if (offRaw.size() == 5 && (offRaw[0] == '+' || offRaw[0] == '-')) {
        tzOffset = std::format("{}:{}", offRaw.substr(0, 3), offRaw.substr(3, 2));
      } else {
        tzOffset = std::string{offRaw};
      }

      auto const formatted = std::format(
        "[{}.{:03d}{}] [critical] [infra.crash] {}{}\n",
        tsBuf.data(),
        ms,
        tzOffset,
        message,
        runId.empty() ? "" : std::format(" run_id={}", runId)
      );

      ofs.write(formatted.data(), static_cast<std::streamsize>(formatted.size()));

      // D8: companion NDJSON record when JSON logging is active.
      // Best-effort, no retry, MAY be absent (e.g. rotation-window open
      // failure) — the .log line above is the durability target.
      auto const ndjsonPath = logging::currentNdjsonFilePath();
      if (ndjsonPath.has_value()) {
        auto ndjsonOfs = std::ofstream(ndjsonPath.value(), std::ios::app);
        if (ndjsonOfs) {
          auto const jsonLine = crash::formatCrashJsonLine(message, runId);
          ndjsonOfs.write(jsonLine.data(), static_cast<std::streamsize>(jsonLine.size()));
        }
      }
      return true;
    } catch (...) {
      if (attempt + 1 < kMaxAttempts) {
        std::this_thread::sleep_for(kRetryDelay);
        continue;
      }
      return false;
    }
  }
  return false;
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
  //   2) tryWriteToLogger — NOTE: posts asynchronously and returns; under
  //      std::_Exit/ExitProcess the async queue is never drained, so this tier
  //      is best-effort only. The direct tier above is the durability guarantee.
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

// Public single-line direct append (tier-1 logic) for paths that bypass all
// spdlog machinery, e.g. the force-exit watchdog before ExitProcess.
auto writeDirectLogLine(std::string_view message) -> bool {
  return tryWriteDirectToLogFile(std::string{message});
}

auto formatCrashJsonLine(std::string_view message, std::string_view runId)
  -> std::string {
  // UTC .sssZ timestamp aligned with JsonFormatter (records in .ndjson must
  // match the documented schema, not the .log pattern).
  auto const now = std::chrono::system_clock::now();
  auto const t = std::chrono::system_clock::to_time_t(now);
  auto const ms = static_cast<int>(
    std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
    % 1000
  );
  auto tm = std::tm{};
#if defined(_WIN32) || defined(_WIN64)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  auto tsBuf = std::array<char, 64>{};
  std::strftime(tsBuf.data(), tsBuf.size(), "%Y-%m-%dT%H:%M:%S", &tm);
  return std::format(
    "{{\"timestamp\":\"{}.{:03d}Z\",\"level\":\"critical\","
    "\"module\":\"infra.crash\",\"source\":\"\","
    "\"message\":{},\"run_id\":{}}}\n",
    tsBuf.data(),
    ms,
    logging::detail::escapeJsonString(message),  // includes the surrounding quotes
    logging::detail::escapeJsonString(runId)
  );
}

}  // namespace crash
