#include "logging/setup.h"

#include "logging/log_tags.h"

#include <spdlog/async.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <memory>
#include <mutex>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

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
// D-03: 源位置直接注入消息体格式 "file.cpp:line"
// D-11: %^%l%$ 提供 level 颜色标记

constexpr auto kLogPattern = "[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] [%n] %v";

// ── 所有模组标签列表 ─────────────────────────────────────────────────────

auto allModuleTags() -> std::vector<char const*> {
    return {
        logtags::APP_ENTRY,
        logtags::APP_PRELUDE,
        logtags::APP_PIPELINE,
        logtags::CMD_CONFIG,
        logtags::VIDEO_ENCODE,
        logtags::VIDEO_PROBE,
        logtags::VIDEO_INFO,
        logtags::VIDEO_OUTPUT,
        logtags::VIDEO_BATCH,
        logtags::VIDEO_PROGRESS,
        logtags::VIDEO_STATE,
        logtags::VIDEO_PROCESS,
        logtags::PICTURE_PROCESS,
        logtags::PICTURE_COMPRESS,
        logtags::PACK_ZIP,
        logtags::PACK_SERVICE,
        logtags::CORE_SCAN,
        logtags::CORE_JOB,
        logtags::CORE_TASK,
        logtags::CORE_PARALLEL,
        logtags::INFRA_TOOLCHAIN,
        logtags::INFRA_CRASH,
        logtags::INFRA_SIGNAL,
        logtags::UTILS_SUBPROCESS,
    };
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
// logging::setup / logging::shutdown
// ═════════════════════════════════════════════════════════════════════════════

namespace logging {

auto setup(LogConfig const& config) -> std::optional<fs::path> {
    // 1. 如果 --verbose 未启用，关闭所有日志
    if (!config.verboseEnabled) {
        spdlog::set_level(spdlog::level::off);
        return std::nullopt;
    }

    // 2. 解析日志目录
    auto logDir = config.customLogDir.has_value()
                    ? config.customLogDir.value()
                    : resolveCommonLogDir();
    auto ec = std::error_code{};
    fs::create_directories(logDir, ec);
    if (ec) {
        spdlog::warn(
            "Cannot create log directory '{}': {}. Fallback to temp directory.",
            logDir.string(),
            ec.message()
        );
        logDir = fs::temp_directory_path() / "encro" / "logs";
        fs::create_directories(logDir, ec);
    }

    // 3. 日志文件路径 (Phase 1 使用现有文件名；Phase 2 改为时间戳命名)
    auto const logFilePath = logDir / "encro.verbose.log";

    // 4. 创建共享 sink
    auto sinks = std::vector<spdlog::sink_ptr>{};
    sinks.emplace_back(
        std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true));

    // 5. 可选的 console sink
    if (config.verboseEchoEnabled) {
        if (config.colorsEnabled) {
            sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        } else {
            sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());
        }
    }

    // 6. 初始化全局 thread pool (复用现有 once_flag 模式)
    static auto poolInitFlag = std::once_flag{};
    std::call_once(poolInitFlag, [] { spdlog::init_thread_pool(8192, 1); });

    // 7. 为每个模组标签创建 named async_logger，共享同一组 sink
    for (auto const* tag : allModuleTags()) {
        auto logger = std::make_shared<spdlog::async_logger>(
            tag,        // logger 名称 = 模块标签
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

    // 8. 设置 default logger (crash handler 通过 default_logger_raw() 访问)
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
}

}  // namespace logging
