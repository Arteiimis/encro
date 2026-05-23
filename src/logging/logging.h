#pragma once

#include "logging/log_tags.h"

#include <spdlog/async.h>
#include <spdlog/spdlog.h>

#include <fmt/format.h>

#include <chrono>
#include <memory>
#include <vector>

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

// ── loggerPtr forward declaration ───────────────────────────────────────────
// ScopedTimer's inline methods call LOG_INFO which references loggerPtr().
// DEFINE_LOGGER (in each .cpp file) provides the definition. This forward
// declaration ensures the compiler knows the signature before parsing
// ScopedTimer's inline bodies.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-internal"
static auto loggerPtr() noexcept -> spdlog::logger*;
#pragma clang diagnostic pop

// ── ScopedTimer (D-08~D-12: RAII stage timing) ──
//
// D-08: Constructor logs "[stage] begin" at LOG_INFO.
//       Destructor logs "[stage] completed in Xms" at LOG_INFO.
// D-09: Freeform std::string_view stage name — descriptive, not machine-parseable.
// D-10: steady_clock for duration measurement — never mixed with system_clock
//       spdlog timestamps (Pitfall #4 prevention).
// D-11: Destructor is noexcept — always completes even during exception unwinding.
// D-12: Nesting naturally supported — each ScopedTimer holds independent start_
//       timepoint. C++ destruction order (reverse of construction) produces
//       correctly ordered begin/complete pairs.
//
// Move-only: copy deleted. Move transfers timing ownership — moved-from
// instances are no-ops on destruction to prevent double-logging.

namespace logging {

class ScopedTimer {
  std::string_view stageName_{};
  std::chrono::steady_clock::time_point start_{};
  bool movedFrom_{false};

public:
  explicit ScopedTimer(std::string_view stageName)
    : stageName_(stageName), start_(std::chrono::steady_clock::now()) {
    LOG_INFO("{} begin", stageName_);
  }

  ~ScopedTimer() noexcept {
    if (movedFrom_) { return; }
    auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start_
    )
                           .count();
    LOG_INFO("{} completed in {}ms", stageName_, elapsed);
  }

  ScopedTimer(ScopedTimer const&) = delete;
  auto operator=(ScopedTimer const&) -> ScopedTimer& = delete;

  ScopedTimer(ScopedTimer&& other) noexcept
    : stageName_(other.stageName_), start_(other.start_) {
    other.movedFrom_ = true;
  }

  auto operator=(ScopedTimer&& other) noexcept -> ScopedTimer& {
    if (this != &other) {
      stageName_ = other.stageName_;
      start_ = other.start_;
      movedFrom_ = false;
      other.movedFrom_ = true;
    }
    return *this;
  }
};

// ── ScopedErrorContext ──
//
// D-07: RAII class that pushes/pops context frames onto a thread-local stack.
//       Mirrors ScopedTimer's move-only / noexcept destructor / movedFrom_ pattern.
// D-01: Thread-local storage — per-thread independent, no cross-thread sharing.
// D-02: Max 16 frames; exceeding drops oldest (FIFO eviction) with truncation marker.
// D-03/D-04: formatContextChain() serializes frames as " [context: stage(detail) > ...]".

struct ContextFrame {
  std::string_view stage;
  std::string_view detail;
};

namespace detail {

inline auto contextStack() -> std::vector<ContextFrame>& {
  thread_local auto stack = std::vector<ContextFrame>{};
  return stack;
}

inline auto truncatedFrameCount() -> int& {
  thread_local auto count = int{0};
  return count;
}

inline auto pushContextFrame(std::string_view stage, std::string_view detail) -> void {
  auto& stack = contextStack();
  if (stack.size() >= 16) {
    stack.erase(stack.begin());
    ++truncatedFrameCount();
  }
  stack.push_back({stage, detail});
}

inline auto popContextFrame() -> void {
  auto& stack = contextStack();
  if (!stack.empty()) { stack.pop_back(); }
}

inline auto resetContextStack() -> void {
  contextStack().clear();
  truncatedFrameCount() = 0;
}

inline auto formatContextChain() -> std::string {
  auto const& stack = contextStack();
  if (stack.empty()) { return ""; }

  std::string chain;
  auto const truncated = truncatedFrameCount();
  if (truncated > 0) {
    chain += "[truncated: ";
    chain += std::to_string(truncated);
    chain += "] > ";
  }

  for (auto i = std::size_t{0}; i < stack.size(); ++i) {
    if (i > 0) { chain += " > "; }
    chain += stack[i].stage;
    if (!stack[i].detail.empty()) {
      chain += "(";
      chain += stack[i].detail;
      chain += ")";
    }
  }

  return " [context: " + chain + "]";
}

}  // namespace detail

class ScopedErrorContext {
  bool movedFrom_{false};

public:
  ScopedErrorContext(std::string_view stage, std::string_view detail) {
    detail::pushContextFrame(stage, detail);
  }

  ~ScopedErrorContext() noexcept {
    if (movedFrom_) { return; }
    detail::popContextFrame();
  }

  ScopedErrorContext(ScopedErrorContext const&) = delete;
  auto operator=(ScopedErrorContext const&) -> ScopedErrorContext& = delete;

  ScopedErrorContext(ScopedErrorContext&& other) noexcept: movedFrom_(false) {
    other.movedFrom_ = true;
  }

  auto operator=(ScopedErrorContext&& other) noexcept -> ScopedErrorContext& {
    if (this != &other) {
      movedFrom_ = false;
      other.movedFrom_ = true;
    }
    return *this;
  }
};

}  // namespace logging
