#include "logging/setup.h"

#include "core/app_context.h"
#include "infra/terminal.h"
#include "logging/json_formatter.h"
#include "logging/log_tags.h"
#include "utils/utils.h"

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Resettable thread-pool init guard; allows re-init after shutdown()
static auto gPoolInitialized = std::atomic<bool>{false};
static auto gPoolInitMutex = std::mutex{};

// ── Single log pattern (D-03 compatible — source location in message body, not %s:%#) ──

constexpr auto kLogPattern = "[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] [%n] %v";

// ── Pass-through counting sink (D6: level_counts for the summary) ──────────
// Sits at the head of the sink chain, increments an atomic per level, forwards
// the record unchanged. One instance shared by all loggers (they all receive
// the same sink vector), so counts are global for the run.

class LevelCountingSink final: public spdlog::sinks::sink {
public:
  explicit LevelCountingSink(spdlog::sink_ptr next): next_(std::move(next)) { }

  auto log(spdlog::details::log_msg const& msg) -> void override {
    ++counts_[static_cast<std::size_t>(msg.level)];
    next_->log(msg);
  }

  auto flush() -> void override { next_->flush(); }

  auto set_pattern(std::string const& pattern) -> void override {
    next_->set_pattern(pattern);
  }

  auto set_formatter(std::unique_ptr<spdlog::formatter> sinkFormatter) -> void override {
    next_->set_formatter(std::move(sinkFormatter));
  }

  auto count(std::size_t level) const -> std::uint64_t {
    return counts_[level].load(std::memory_order_relaxed);
  }

private:
  spdlog::sink_ptr next_;
  std::array<std::atomic<std::uint64_t>, spdlog::level::n_levels> counts_{};
};

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
// ── Env var reading (Windows) ───────────────────────────────────────────────

#if defined(_WIN32) || defined(_WIN64)
auto readWindowsEnvPath(char const* name) -> std::optional<fs::path> {
  auto value = std::unique_ptr<char>{};
  auto size = std::size_t{0};
  // Note: the out_ptr write-back happens when the temporary is destroyed, so
  // `value` must not be inspected inside the same full-expression.
  auto const rc = _dupenv_s(std::out_ptr(value), &size, name);
  if (rc != 0 || value == nullptr || size == 0) { return std::nullopt; }

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
  if (
    auto const* xdgState = std::getenv("XDG_STATE_HOME");
    xdgState != nullptr && *xdgState != '\0'
  ) {
    return fs::path{xdgState} / "encro" / "logs";
  }
  if (auto const* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return fs::path{home} / ".local" / "state" / "encro" / "logs";
  }
#endif

  return fs::temp_directory_path() / "encro" / "logs";
}

// ═════════════════════════════════════════════════════════════════════════════
// logging::setup / logging::shutdown
// ═════════════════════════════════════════════════════════════════════════════

namespace logging {

// ── Current log file path (D-13: crash handler integration) ────────────────

static auto gCurrentLogFilePath = std::optional<fs::path>{};
static auto gCurrentNdjsonFilePath = std::optional<fs::path>{};

// ── Run id (D3: bootstrap + job-state adoption) ────────────────────────────

static auto gRunIdMutex = std::mutex{};
static auto gRunId = std::string{};

// Lock-free run-id snapshot for crash handlers (D8): an atomic pointer to an
// immutable copy. A reader sees either the previous or the new id, never a
// hybrid; the copies are intentionally never freed (~3 per process lifetime,
// ~40 bytes each) so the pointer stays valid forever. The crash handler must
// never take a lock — the crashing thread may hold gRunIdMutex at death.
static auto gRunIdSnapshot = std::atomic<char const*>{nullptr};

auto updateRunIdSnapshot(std::string_view id) -> void {
  auto* copy = new std::string{id};
  gRunIdSnapshot.store(copy->c_str(), std::memory_order_release);
}

auto runIdSnapshot() -> std::string_view {
  auto const* text = gRunIdSnapshot.load(std::memory_order_acquire);
  if (text == nullptr) { return {}; }
  return std::string_view{text};
}

// ── Counting sink + summary (D6) ───────────────────────────────────────────

static auto gCountingSink = std::shared_ptr<LevelCountingSink>{};
// Snapshot taken at shutdown so levelCounts() stays queryable after the
// sink chain (and its file handles) is released.
static auto
  gLevelCountSnapshot =  // NOLINT(bugprone-throwing-static-initialization): std::map default ctor is noexcept
  std::map<std::string, std::uint64_t>{};

namespace {

auto levelName(std::size_t level) -> std::string {
  auto const sv =
    spdlog::level::to_string_view(static_cast<spdlog::level::level_enum>(level));
  return std::string{sv.data(), sv.size()};
}

auto readLiveCounts() -> std::map<std::string, std::uint64_t> {
  auto counts = std::map<std::string, std::uint64_t>{};
  if (gCountingSink == nullptr) { return counts; }
  for (auto level = std::size_t{0}; level < spdlog::level::n_levels; ++level) {
    auto const count = gCountingSink->count(level);
    if (count > 0) { counts[levelName(level)] = count; }
  }
  return counts;
}

}  // namespace

auto levelCounts() -> std::map<std::string, std::uint64_t> {
  if (gCountingSink != nullptr) { return readLiveCounts(); }
  return gLevelCountSnapshot;
}

auto logRunSummary(SummaryData const& data) -> void {
  // key=value body, values guaranteed space-free (log path and level_counts
  // are appended last; the formatter regenerates them authoritatively)
  auto body = std::string{"RUN SUMMARY: status="};
  body += data.status;
  if (data.jobId.has_value()) { body += " jobId=" + data.jobId.value(); }
  if (data.tasksTotal.has_value()) {
    body += " tasks_total=" + std::to_string(data.tasksTotal.value());
  }
  if (data.tasksFailed.has_value()) {
    body += " tasks_failed=" + std::to_string(data.tasksFailed.value());
  }
  if (data.elapsedMs.has_value()) {
    body += " elapsed_ms=" + std::to_string(data.elapsedMs.value());
  }
  // Level counts (JSON, space-free) and log path complete the human-readable
  // line so it carries the same information as the NDJSON summary object.
  body += " level_counts={";
  auto first = true;
  for (auto const& [level, count]: levelCounts()) {
    if (!first) { body += ','; }
    first = false;
    body += level + ":" + std::to_string(count);
  }
  body += "} log=";
  body += currentLogFilePath().value_or(fs::path{}).string();

  auto* logger = spdlog::default_logger_raw();
  if (logger != nullptr) { logger->info(body); }
}

auto runId() -> std::string {
  auto lock = std::scoped_lock{gRunIdMutex};
  if (gRunId.empty()) {
    gRunId = getUUID();
    updateRunIdSnapshot(gRunId);
  }
  return gRunId;
}

auto setRunId(std::string id) -> void {
  auto lock = std::scoped_lock{gRunIdMutex};
  gRunId = std::move(id);
  updateRunIdSnapshot(gRunId);
}

// NOLINTNEXTLINE(readability-function-size): linear sink wiring; numbered blocks are self-documenting
auto setup(LogConfig const& config) -> std::optional<fs::path> {
  gCurrentLogFilePath = std::nullopt;
  gCurrentNdjsonFilePath = std::nullopt;

  // Bootstrap run id for this run; job state adopts it later (or overrides on resume)
  setRunId(getUUID());
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
      // D8: remember the companion path for crash-handler direct writes
      gCurrentNdjsonFilePath = ndjsonPath;
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

  // 5b. Counting sink at the head of the chain (shared by all loggers)
  if (!sinks.empty()) {
    gCountingSink = std::make_shared<LevelCountingSink>(sinks.front());
    sinks.insert(sinks.begin(), gCountingSink);
  } else {
    gCountingSink = nullptr;
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

  // Periodic flush bounds the loss window on hard termination (kill/power loss)
  // to at most the interval's worth of buffered non-error lines
  // (recent log lines survive abnormal termination).
  spdlog::flush_every(std::chrono::seconds{1});

  if (fileSinkEnabled) { spdlog::debug("File logging enabled."); }

  return logFilePath;
}

auto shutdown() -> void {
  spdlog::shutdown();
  gPoolInitialized = false;
  gCurrentLogFilePath = std::nullopt;
  gCurrentNdjsonFilePath = std::nullopt;
  // Snapshot counts, then release the chain so wrapped file sinks flush and
  // close their handles (kept alive only by the global shared_ptr).
  gLevelCountSnapshot = readLiveCounts();
  gCountingSink.reset();
  // Reset so tests get a fresh lazy id after teardown
  setRunId("");
}

auto currentLogFilePath() -> std::optional<fs::path> {
  return gCurrentLogFilePath;
}

auto currentNdjsonFilePath() -> std::optional<fs::path> {
  return gCurrentNdjsonFilePath;
}

// ── Forensic context state ──────────────────────────────────────────────────

static auto gForensicAppCtx = std::atomic<void*>{nullptr};
static auto
  gForensicSnapshotData =  // NOLINT(bugprone-throwing-static-initialization): EnvironmentSnapshot is noexcept-default-constructible
  EnvironmentSnapshot{};

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
