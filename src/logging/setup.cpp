#include "logging/setup.h"

#include "core/app_context.h"
#include "infra/terminal.h"
#include "logging/json_formatter.h"
#include "logging/log_tags.h"

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <mutex>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Resettable thread-pool init guard; allows re-init after shutdown()
static auto gPoolInitialized = std::atomic<bool>{false};
static auto gPoolInitMutex = std::mutex{};

// ── Env var reading (Windows) ───────────────────────────────────────────────

#if defined(_WIN32) || defined(_WIN64)
auto readWindowsEnvPath(char const* name) -> std::optional<fs::path> {
  auto value = std::unique_ptr<char>{};
  auto size = std::size_t{0};
  if (_dupenv_s(std::out_ptr(value), &size, name) != 0 || value == nullptr || size == 0) {
    return std::nullopt;
  }

  auto result = fs::path{value.get()};
  if (result.empty()) { return std::nullopt; }

  return result;
}
#endif

// ── Log directory resolution (migrated from prelude.cpp, logic unchanged) ──

auto resolveCommonLogDir() -> fs::path {
#if defined(_WIN32) || defined(_WIN64)
  if (
    auto const localAppData = readWindowsEnvPath("LOCALAPPDATA"); localAppData.has_value()
  ) {
    return localAppData.value() / "encro" / "logs";
  }

  if (auto const appData = readWindowsEnvPath("APPDATA"); appData.has_value()) {
    return appData.value() / "encro" / "logs";
  }
#else
  if (auto const* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return fs::path{home} / ".local" / "state" / "encro" / "logs";
  }
#endif

  return fs::temp_directory_path() / "encro" / "logs";
}

// ── Single log pattern (D-03 compatible — source location in message body, not %s:%#) ──

constexpr auto kLogPattern = "[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] [%n] %v";

// ── All module tag list ─────────────────────────────────────────────────────

auto allModuleTags() -> std::vector<char const*> {
  return {
    logtags::APP_ENTRY,       logtags::APP_PRELUDE,      logtags::APP_PIPELINE,
    logtags::CMD_CONFIG,      logtags::VIDEO_ENCODE,     logtags::VIDEO_PROBE,
    logtags::VIDEO_INFO,      logtags::VIDEO_OUTPUT,     logtags::VIDEO_BATCH,
    logtags::VIDEO_PROGRESS,  logtags::VIDEO_STATE,      logtags::VIDEO_PROCESS,
    logtags::PICTURE_PROCESS, logtags::PICTURE_COMPRESS, logtags::PACK_ZIP,
    logtags::PACK_SERVICE,    logtags::CORE_SCAN,        logtags::CORE_JOB,
    logtags::CORE_TASK,       logtags::CORE_PARALLEL,    logtags::INFRA_TOOLCHAIN,
    logtags::INFRA_CRASH,     logtags::INFRA_SIGNAL,     logtags::UTILS_SUBPROCESS,
  };
}

// ── Log file retention cleanup (D-04~D-07) ─────────────────────────────────

auto retainRecentLogs(fs::path const& logDir, int const maxKeep) -> std::size_t {
  try {
    auto entries = std::vector<fs::path>{};
    auto ec = std::error_code{};
    for (auto const& entry: fs::directory_iterator{logDir, ec}) {
      if (ec) { break; }
      if (!entry.is_regular_file()) { continue; }
      auto const filename = entry.path().filename().string();
      if (!filename.starts_with("encro_")) { continue; }
      auto hasLogExt = filename.find(".log") != std::string::npos;
      auto hasNdjsonExt = filename.find(".ndjson") != std::string::npos;
      if (!hasLogExt && !hasNdjsonExt) { continue; }
      entries.push_back(entry.path());
    }

    if (entries.size() <= static_cast<std::size_t>(maxKeep)) { return 0; }

    // D-06: sort by filename lexicographically — YYYYMMDD_HHMMSS format = chronological order
    std::sort(entries.begin(), entries.end());

    auto const total = entries.size();
    auto const toRemove = total - static_cast<std::size_t>(maxKeep);

    for (auto i = std::size_t{0}; i < toRemove; ++i) {
      auto ec2 = std::error_code{};
      fs::remove(entries[i], ec2);
    }

    return toRemove;
  } catch (...) {
    // Best-effort cleanup — filesystem errors do not propagate
    return 0;
  }
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
// logging::setup / logging::shutdown
// ═════════════════════════════════════════════════════════════════════════════

namespace logging {

// ── Current log file path (D-13: crash handler integration) ────────────────

static auto gCurrentLogFilePath = std::optional<fs::path>{};

auto setup(LogConfig const& config) -> std::optional<fs::path> {
  gCurrentLogFilePath = std::nullopt;

  // 1. Resolve log directory (D-21: hardened fallback chain)
  auto logDir =
    config.customLogDir.has_value() ? config.customLogDir.value() : resolveCommonLogDir();
  auto ec = std::error_code{};
  fs::create_directories(logDir, ec);

  auto fileSinkEnabled = true;

  if (ec) {
    // Main dir creation failed — fall back to the temp directory
    logDir = fs::temp_directory_path() / "encro" / "logs";
    auto ec2 = std::error_code{};
    fs::create_directories(logDir, ec2);
    if (!ec2) {
      // D-22: warn the user when falling back to the temp directory
      terminal::println(
        terminal::MessageKind::Warning,
        "Warning: Using temporary log directory: {}",
        terminal::path(logDir)
      );
    } else {
      // D-21: temp dir creation also failed — skip file sink, console only
      terminal::eprintln(
        terminal::MessageKind::Error,
        "Cannot create log directory; logging to console only."
      );
      fileSinkEnabled = false;
    }
  }

  // 3. Create file sink (D-01~D-03, D-17~D-18) — always enabled
  auto sinks = std::vector<spdlog::sink_ptr>{};
  auto logFilePath = std::optional<fs::path>{};

  if (fileSinkEnabled) {
    // D-04~D-07: keep the 10 most recent log files (before creating a new one)
    retainRecentLogs(logDir, 10);

    // D-01: generate timestamped filename encro_YYYYMMDD_HHMMSS.log
    auto const now = std::chrono::system_clock::now();
    auto const t = std::chrono::system_clock::to_time_t(now);
    auto tm = std::tm{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    auto tsBuf = std::array<char, 64>{};
    std::strftime(tsBuf.data(), tsBuf.size(), "encro_%Y%m%d_%H%M%S.log", &tm);
    auto filePath = logDir / tsBuf.data();

    // D-02: PID collision detection — multiple starts within the same second
    if (fs::exists(filePath)) {
      std::strftime(tsBuf.data(), tsBuf.size(), "encro_%Y%m%d_%H%M%S", &tm);
#if defined(_WIN32) || defined(_WIN64)
      auto const pid = _getpid();
#else
      auto const pid = getpid();
#endif
      filePath = logDir / fmt::format("{}_{}.log", tsBuf.data(), pid);
    }

    logFilePath = filePath;

    // D-17~D-18: human-readable rotating file sink
    auto hrSink = std::make_shared<
      spdlog::sinks::rotating_file_sink_mt
    >(filePath.string(), 10 * 1024 * 1024, 3);
    hrSink->set_pattern(kLogPattern);
    sinks.emplace_back(std::move(hrSink));

    // D-03/D-04: Companion NDJSON sink with JsonFormatter when --log-json is active
    if (config.jsonEnabled) {
      auto ndjsonPath = filePath;
      ndjsonPath.replace_extension(".ndjson");
      auto jsonSink = std::make_shared<
        spdlog::sinks::rotating_file_sink_mt
      >(ndjsonPath.string(), 10 * 1024 * 1024, 3);
      jsonSink->set_formatter(std::make_unique<logging::JsonFormatter>());
      sinks.emplace_back(std::move(jsonSink));
    }

    // D-13: store current log file path for crash handler direct writes
    gCurrentLogFilePath = filePath;
  }

  // 4. Optional console sink
  if (config.echoEnabled) {
    if (config.colorsEnabled) {
      auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
      consoleSink->set_pattern(kLogPattern);
      sinks.emplace_back(std::move(consoleSink));
    } else {
      auto consoleSink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
      consoleSink->set_pattern(kLogPattern);
      sinks.emplace_back(std::move(consoleSink));
    }
  }

  // 5. Init global thread pool (resettable flag; supports setup/shutdown/setup in tests)
  {
    auto lock = std::lock_guard{gPoolInitMutex};
    if (!gPoolInitialized.exchange(true)) { spdlog::init_thread_pool(8192, 1); }
  }

  // 6. Create one named async_logger per module tag, sharing the same sinks
  for (auto const* tag: allModuleTags()) {
    auto logger = std::make_shared<spdlog::async_logger>(
      tag,
      sinks.begin(),
      sinks.end(),
      spdlog::thread_pool(),
      spdlog::async_overflow_policy::block
    );
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::err);
    spdlog::register_logger(std::move(logger));
  }

  // 7. Set default logger (crash handler accesses via default_logger_raw())
  auto defaultLogger = std::make_shared<spdlog::async_logger>(
    "encro",
    sinks.begin(),
    sinks.end(),
    spdlog::thread_pool(),
    spdlog::async_overflow_policy::block
  );
  defaultLogger->set_level(spdlog::level::debug);
  defaultLogger->flush_on(spdlog::level::err);
  spdlog::set_default_logger(std::move(defaultLogger));
  spdlog::set_level(spdlog::level::debug);

  if (fileSinkEnabled) { spdlog::debug("File logging enabled."); }

  return logFilePath;
}

auto shutdown() -> void {
  spdlog::shutdown();
  gPoolInitialized = false;
  gCurrentLogFilePath = std::nullopt;
}

auto currentLogFilePath() -> std::optional<fs::path> {
  return gCurrentLogFilePath;
}

// ── Forensic context state ──────────────────────────────────────────────────

static auto gForensicAppCtx = std::atomic<void*>{nullptr};
static auto gForensicSnapshotData = EnvironmentSnapshot{};

auto setForensicAppContext(void* appCtx) -> void {
  gForensicAppCtx.store(appCtx, std::memory_order_release);
}

auto updateForensicSnapshot(
  int const activeSlots,
  int const totalSlots,
  int const pending,
  int const finished
) -> void {
  gForensicSnapshotData.hasEncodingContext = true;
  gForensicSnapshotData.activeSlots = activeSlots;
  gForensicSnapshotData.totalSlots = totalSlots;
  gForensicSnapshotData.pending = pending;
  gForensicSnapshotData.finished = finished;
}

auto setForensicSnapshotData(EnvironmentSnapshot const& data) -> void {
  gForensicSnapshotData = data;
}

auto clearForensicSnapshotData() -> void {
  gForensicAppCtx.store(nullptr, std::memory_order_release);
  gForensicSnapshotData = EnvironmentSnapshot{};
}

auto captureEnvironmentSnapshot() -> std::string {
  auto* const appCtx =
    static_cast<appctx::AppContext*>(gForensicAppCtx.load(std::memory_order_acquire));
  if (appCtx == nullptr) { return ""; }

  auto const processType = appCtx->config.processType;

  if (!gForensicSnapshotData.hasEncodingContext) {
    return fmt::format("Environment: pipeline={} (no encoding slots)", processType);
  }

  auto const& data = gForensicSnapshotData;
  auto subprocessStr = std::string{};
  if (data.subprocessPid.has_value()) {
    subprocessStr = fmt::format("subprocess=[pid={}", data.subprocessPid.value());
    if (data.subprocessCmdline.has_value()) {
      subprocessStr += fmt::format(" cmd='{}'", data.subprocessCmdline.value());
    }
    subprocessStr += "]";
  }

  return fmt::format(
    "Environment: active-slots={}/{} pending={} finished={}{}{}",
    data.activeSlots,
    data.totalSlots,
    data.pending,
    data.finished,
    subprocessStr.empty() ? "" : " ",
    subprocessStr
  );
}

}  // namespace logging
