#include "logging/setup.h"

#include "core/app_context.h"
#include "infra/terminal.h"
#include "logging/json_formatter.h"
#include "logging/log_tags.h"

#include <spdlog/async.h>
#include <spdlog/logger.h>
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

// 可重置的 thread-pool 初始化守卫，shutdown() 后允许重新初始化
static auto gPoolInitialized = std::atomic<bool>{false};
static auto gPoolInitMutex = std::mutex{};

// ── 环境变量读取 (Windows) ──────────────────────────────────────────────────

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

// ── 日志目录解析 (从 prelude.cpp 迁移，逻辑不变) ───────────────────────────

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

// ── 单一日志 pattern (D-03 兼容 —— 源位置在消息体中，不用 %s:%#) ──────

constexpr auto kLogPattern = "[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] [%n] %v";

// ── 所有模组标签列表 ─────────────────────────────────────────────────────

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

// ── 日志文件保留清理 (D-04~D-07) ─────────────────────────────────────────

auto retainRecentLogs(fs::path const& logDir, int const maxKeep) -> std::size_t {
  try {
    auto entries = std::vector<fs::path>{};
    auto ec = std::error_code{};
    for (auto const& entry: fs::directory_iterator{logDir, ec}) {
      if (ec) { break; }
      if (!entry.is_regular_file()) { continue; }
      auto const filename = entry.path().filename().string();
      if (!filename.starts_with("encro_")) { continue; }
      auto hasLogExt    = filename.find(".log")   != std::string::npos;
      auto hasNdjsonExt = filename.find(".ndjson") != std::string::npos;
      if (!hasLogExt && !hasNdjsonExt) { continue; }
      entries.push_back(entry.path());
    }

    if (entries.size() <= static_cast<std::size_t>(maxKeep)) { return 0; }

    // D-06: 按文件名字典序排序 — YYYYMMDD_HHMMSS 格式 = 时间顺序
    std::sort(entries.begin(), entries.end());

    auto const total = entries.size();
    auto const toRemove = total - static_cast<std::size_t>(maxKeep);

    for (auto i = std::size_t{0}; i < toRemove; ++i) {
      auto ec2 = std::error_code{};
      fs::remove(entries[i], ec2);
    }

    return toRemove;
  } catch (...) {
    // 尽最大努力清理 — 文件系统错误不传播
    return 0;
  }
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
// logging::setup / logging::shutdown
// ═════════════════════════════════════════════════════════════════════════════

namespace logging {

// ── 当前日志文件路径 (D-13: crash handler 集成) ──────────────────────────

static auto gCurrentLogFilePath = std::optional<fs::path>{};

auto setup(LogConfig const& config) -> std::optional<fs::path> {
  // 1. 如果 --verbose 和 --log-json 都未启用，关闭所有日志
  if (!config.verboseEnabled && !config.jsonEnabled) {
    spdlog::set_level(spdlog::level::off);
    return std::nullopt;
  }

  // 2. 解析日志目录 (D-21: 强化回退链)
  auto logDir =
    config.customLogDir.has_value() ? config.customLogDir.value() : resolveCommonLogDir();
  auto ec = std::error_code{};
  fs::create_directories(logDir, ec);

  auto fileSinkEnabled = true;

  if (ec) {
    // 主目录创建失败 — 回退到临时目录
    logDir = fs::temp_directory_path() / "encro" / "logs";
    auto ec2 = std::error_code{};
    fs::create_directories(logDir, ec2);
    if (!ec2) {
      // D-22: 回退到临时目录时警告用户
      terminal::println(
        terminal::MessageKind::Warning,
        "Warning: Using temporary log directory: {}",
        terminal::path(logDir)
      );
    } else {
      // D-21: 临时目录也创建失败 — 跳过文件 sink，仅 console
      terminal::eprintln(
        terminal::MessageKind::Error,
        "Cannot create log directory; logging to console only."
      );
      fileSinkEnabled = false;
    }
  }

  // 3. 创建文件 sink (D-01~D-03, D-17~D-18)
  auto sinks = std::vector<spdlog::sink_ptr>{};
  auto logFilePath = std::optional<fs::path>{};

  if (fileSinkEnabled) {
    // D-04~D-07: 保留最近 10 个日志文件 (在创建新文件之前)
    retainRecentLogs(logDir, 10);

    // D-01: 生成时间戳文件名 encro_YYYYMMDD_HHMMSS.log
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

    // D-02: PID 冲突检测 — 同一秒内多次启动
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

    // D-17~D-18: human-readable rotating file sink (only when verbose)
    if (config.verboseEnabled) {
      sinks.emplace_back(
        std::make_shared<
          spdlog::sinks::rotating_file_sink_mt
        >(filePath.string(), 10 * 1024 * 1024, 3)
      );
    }

    // D-03/D-04: Companion NDJSON sink with JsonFormatter when --log-json is active
    if (config.jsonEnabled) {
      auto ndjsonPath = filePath;
      ndjsonPath.replace_extension(".ndjson");
      auto jsonSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        ndjsonPath.string(), 10 * 1024 * 1024, 3);
      jsonSink->set_formatter(std::make_unique<logging::JsonFormatter>());
      sinks.emplace_back(std::move(jsonSink));
    }

    // D-13: 存储当前日志文件路径供 crash handler 直接写入
    gCurrentLogFilePath = filePath;
  }

  // 4. 可选的 console sink
  if (config.verboseEchoEnabled) {
    if (config.colorsEnabled) {
      sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    } else {
      sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());
    }
  }

  // 5. 初始化全局 thread pool (可重置标志，支持测试环境中 setup/shutdown/setup)
  {
    auto lock = std::lock_guard{gPoolInitMutex};
    if (!gPoolInitialized.exchange(true)) { spdlog::init_thread_pool(8192, 1); }
  }

  // 6. 为每个模组标签创建 named async_logger，共享同一组 sink
  for (auto const* tag: allModuleTags()) {
    auto logger = std::make_shared<spdlog::async_logger>(
      tag,
      sinks.begin(),
      sinks.end(),
      spdlog::thread_pool(),
      spdlog::async_overflow_policy::block
    );
    logger->set_pattern(kLogPattern);
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::err);
    spdlog::register_logger(std::move(logger));
  }

  // 7. 设置 default logger (crash handler 通过 default_logger_raw() 访问)
  auto defaultLogger = std::make_shared<spdlog::async_logger>(
    "encro",
    sinks.begin(),
    sinks.end(),
    spdlog::thread_pool(),
    spdlog::async_overflow_policy::block
  );
  defaultLogger->set_pattern(kLogPattern);
  defaultLogger->set_level(spdlog::level::debug);
  defaultLogger->flush_on(spdlog::level::err);
  spdlog::set_default_logger(std::move(defaultLogger));
  spdlog::set_level(spdlog::level::debug);

  spdlog::debug("Verbose logging enabled.");

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
static auto gForensicExecCtx = std::atomic<void*>{nullptr};
static auto gForensicSnapshotData = EnvironmentSnapshot{};

auto setForensicAppContext(void* appCtx) -> void {
  gForensicAppCtx.store(appCtx, std::memory_order_release);
}

auto setForensicExecContext(void* execCtx) -> void {
  gForensicExecCtx.store(execCtx, std::memory_order_release);
}

auto setForensicSnapshotData(EnvironmentSnapshot const& data) -> void {
  gForensicSnapshotData = data;
}

auto clearForensicSnapshotData() -> void {
  gForensicAppCtx.store(nullptr, std::memory_order_release);
  gForensicExecCtx.store(nullptr, std::memory_order_release);
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
