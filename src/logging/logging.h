#pragma once

#include "logging/log_tags.h"

#include <spdlog/async.h>
#include <spdlog/spdlog.h>

#include <fmt/format.h>

#include <memory>

// ── 短文件名提取 ────────────────────────────────────────────────────────────
// clang-cl 上 __FILE__ 默认仅返回文件名 (无路径)，
// 但若未来开启 /FC 或 -fmacro-prefix-map，可退回到运行时截取。
// 运行时截取是 constexpr-friendly: 编译器对字符串字面量完全优化掉。

namespace logging::detail {

[[nodiscard]] inline auto shortFile(char const* path) noexcept -> char const* {
  auto const* last = path;
  for (auto const* p = path; *p != '\0'; ++p) {
    if (*p == '/' || *p == '\\') { last = p + 1; }
  }
  return last;
}

}  // namespace logging::detail

// ── DEFINE_LOGGER ──────────────────────────────────────────────────────────
// 每个 .cpp 文件顶部调用一次:
//   DEFINE_LOGGER(logtags::VIDEO_ENCODE)
//
// 展开为一个 file-static 函数 loggerPtr()，每次调用检查 spdlog registry。
// 不缓存指针 — 测试环境中 logging::shutdown() 会销毁 logger，
// 缓存会导致悬垂指针。
//
// 回退顺序: named logger → default_logger → silent no-op logger
// 最后一级回退确保 loggerPtr() 永不返回 null。
//
// D-05: 每个 .cpp 文件一个 named logger
//
// 使用裸指针而非 shared_ptr — logger 生命周期由 spdlog registry 保证。

#define DEFINE_LOGGER(tag)                                                     \
  static auto loggerPtr() noexcept -> spdlog::logger* {                        \
    if (auto* p = spdlog::get(tag).get()) return p;                            \
    if (auto* p = spdlog::default_logger_raw()) return p;                      \
    static auto fallback = std::make_shared<spdlog::logger>("__encro_null__"); \
    return fallback.get();                                                     \
  }

// ── 日志宏 (D-01: 自定义封装层，内部使用 SPDLOG_LOGGER_CALL) ─────────────
//
// D-02: LOG_INFO 命名，不加 ENCRO_ 前缀
// D-03: 源位置注入消息体，格式 "file.cpp:128"
// SPDLOG_LOGGER_CALL 内部已处理 SPDLOG_ACTIVE_LEVEL 剥离
//
// 关键设计:
//   - fmt::format(__VA_ARGS__) 在 call site 预格式化 — 避免 async TLS 问题
//   - 源位置字符串完全复制到消息体中 (无悬垂指针风险)
//   - 不需要 do { ... } while(0) 包装 — SPDLOG_LOGGER_CALL 自身是完整表达式
//
// Pattern reference:
//   %n → named logger 名称 → 模块标签 (如 "video.encode")
//   %v → 消息体 (已含 "[file:line] actual message")

#define LOG_TRACE(...)                    \
  SPDLOG_LOGGER_CALL(                     \
    loggerPtr(),                          \
    spdlog::level::trace,                 \
    "[{}:{}] {}",                         \
    logging::detail::shortFile(__FILE__), \
    __LINE__,                             \
    fmt::format(__VA_ARGS__)              \
  )

#define LOG_DEBUG(...)                    \
  SPDLOG_LOGGER_CALL(                     \
    loggerPtr(),                          \
    spdlog::level::debug,                 \
    "[{}:{}] {}",                         \
    logging::detail::shortFile(__FILE__), \
    __LINE__,                             \
    fmt::format(__VA_ARGS__)              \
  )

#define LOG_INFO(...)                     \
  SPDLOG_LOGGER_CALL(                     \
    loggerPtr(),                          \
    spdlog::level::info,                  \
    "[{}:{}] {}",                         \
    logging::detail::shortFile(__FILE__), \
    __LINE__,                             \
    fmt::format(__VA_ARGS__)              \
  )

#define LOG_WARN(...)                     \
  SPDLOG_LOGGER_CALL(                     \
    loggerPtr(),                          \
    spdlog::level::warn,                  \
    "[{}:{}] {}",                         \
    logging::detail::shortFile(__FILE__), \
    __LINE__,                             \
    fmt::format(__VA_ARGS__)              \
  )

#define LOG_ERROR(...)                    \
  SPDLOG_LOGGER_CALL(                     \
    loggerPtr(),                          \
    spdlog::level::err,                   \
    "[{}:{}] {}",                         \
    logging::detail::shortFile(__FILE__), \
    __LINE__,                             \
    fmt::format(__VA_ARGS__)              \
  )

#define LOG_CRITICAL(...)                 \
  SPDLOG_LOGGER_CALL(                     \
    loggerPtr(),                          \
    spdlog::level::critical,              \
    "[{}:{}] {}",                         \
    logging::detail::shortFile(__FILE__), \
    __LINE__,                             \
    fmt::format(__VA_ARGS__)              \
  )
