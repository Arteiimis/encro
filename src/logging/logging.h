#pragma once

#include "logging/log_tags.h"

#include <spdlog/async.h>
#include <spdlog/spdlog.h>

#include <fmt/format.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// ── Short file name extraction ──────────────────────────────────────────────
// On clang-cl, __FILE__ is just the file name (no path) by default.
// If /FC or -fmacro-prefix-map is enabled later, fall back to runtime extraction.
// Runtime extraction is constexpr-friendly: compilers fully optimize string literals.

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
// Call once at the top of each .cpp file:
//   DEFINE_LOGGER(logtags::VIDEO_ENCODE)
//
// Expands to a file-static loggerPtr() function that consults the spdlog
// registry on every call. The pointer is not cached — logging::shutdown()
// destroys loggers in tests, and caching would leave a dangling pointer.
//
// Fallback order: named logger → default_logger → silent no-op logger.
// The last fallback guarantees loggerPtr() never returns null.
//
// D-05: one named logger per .cpp file
//
// Uses a raw pointer instead of shared_ptr — logger lifetime is owned by the
// spdlog registry.

#define DEFINE_LOGGER(tag)                                  \
  inline static auto encroLoggerFallback =                  \
    std::make_shared<spdlog::logger>("__encro_null__");     \
  static auto loggerPtr() noexcept -> spdlog::logger* {     \
    try {                                                   \
      if (auto* p = spdlog::get(tag).get()) return p;       \
      if (auto* p = spdlog::default_logger_raw()) return p; \
    } catch (...) { }                                       \
    return encroLoggerFallback.get();                       \
  }

// ── Log macros (D-01: custom wrapper layer over SPDLOG_LOGGER_CALL) ────────
//
// D-02: LOG_INFO naming, no ENCRO_ prefix
// D-03: source location injected into the message, format "file.cpp:128"
//
// Key design:
//   - fmt::format(__VA_ARGS__) pre-formats at the call site — avoids async TLS issues
//   - source location is copied fully into the message (no dangling pointer risk)
//   - the message is fully formatted before calling logger->log(source_loc, level,
//     string_view_t) — the non-template overload, which avoids instantiating
//     spdlog's format_string path. That path converts fmt::format_string to
//     string_view via fmt 12's deprecated operator (spdlog lacks the fmt-path
//     to_string_view overload; tracked upstream: gabime/spdlog#3631).
//     Switch back to SPDLOG_LOGGER_CALL once spdlog supports fmt 12.
//
// Pattern reference:
//   %n → named logger name → module tag (e.g. "video.encode")
//   %v → message body (already contains "[file:line] actual message")

#define LOG_TRACE(...)                                       \
  loggerPtr()->log(                                          \
    spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
    spdlog::level::trace,                                    \
    fmt::format(                                             \
      "[{}:{}] {}{}",                                        \
      logging::detail::shortFile(__FILE__),                  \
      __LINE__,                                              \
      fmt::format(__VA_ARGS__),                              \
      logging::detail::formatAttributeChain()                \
    )                                                        \
  )

#define LOG_DEBUG(...)                                       \
  loggerPtr()->log(                                          \
    spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
    spdlog::level::debug,                                    \
    fmt::format(                                             \
      "[{}:{}] {}{}",                                        \
      logging::detail::shortFile(__FILE__),                  \
      __LINE__,                                              \
      fmt::format(__VA_ARGS__),                              \
      logging::detail::formatAttributeChain()                \
    )                                                        \
  )

#define LOG_INFO(...)                                        \
  loggerPtr()->log(                                          \
    spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
    spdlog::level::info,                                     \
    fmt::format(                                             \
      "[{}:{}] {}{}",                                        \
      logging::detail::shortFile(__FILE__),                  \
      __LINE__,                                              \
      fmt::format(__VA_ARGS__),                              \
      logging::detail::formatAttributeChain()                \
    )                                                        \
  )

#define LOG_WARN(...)                                        \
  loggerPtr()->log(                                          \
    spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
    spdlog::level::warn,                                     \
    fmt::format(                                             \
      "[{}:{}] {}{}",                                        \
      logging::detail::shortFile(__FILE__),                  \
      __LINE__,                                              \
      fmt::format(__VA_ARGS__),                              \
      logging::detail::formatAttributeChain()                \
    )                                                        \
  )

#define LOG_ERROR(...)                                                       \
  do {                                                                       \
    auto const __encro_ctx_chain = logging::detail::formatContextChain();    \
    auto const __encro_attr_chain = logging::detail::formatAttributeChain(); \
    loggerPtr()->log(                                                        \
      spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},               \
      spdlog::level::err,                                                    \
      fmt::format(                                                           \
        "[{}:{}] {}{}{}",                                                    \
        logging::detail::shortFile(__FILE__),                                \
        __LINE__,                                                            \
        fmt::format(__VA_ARGS__),                                            \
        __encro_ctx_chain,                                                   \
        __encro_attr_chain                                                   \
      )                                                                      \
    );                                                                       \
    auto const __encro_snapshot = logging::captureEnvironmentSnapshot();     \
    if (!__encro_snapshot.empty()) {                                         \
      loggerPtr()->log(                                                      \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},             \
        spdlog::level::info,                                                 \
        fmt::format(                                                         \
          "[{}:{}] {}{}",                                                    \
          logging::detail::shortFile(__FILE__),                              \
          __LINE__,                                                          \
          __encro_snapshot,                                                  \
          logging::detail::formatAttributeChain()                            \
        )                                                                    \
      );                                                                     \
    }                                                                        \
  } while (0)

#define LOG_CRITICAL(...)                                                    \
  do {                                                                       \
    auto const __encro_ctx_chain = logging::detail::formatContextChain();    \
    auto const __encro_attr_chain = logging::detail::formatAttributeChain(); \
    loggerPtr()->log(                                                        \
      spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},               \
      spdlog::level::critical,                                               \
      fmt::format(                                                           \
        "[{}:{}] {}{}{}",                                                    \
        logging::detail::shortFile(__FILE__),                                \
        __LINE__,                                                            \
        fmt::format(__VA_ARGS__),                                            \
        __encro_ctx_chain,                                                   \
        __encro_attr_chain                                                   \
      )                                                                      \
    );                                                                       \
    auto const __encro_snapshot = logging::captureEnvironmentSnapshot();     \
    if (!__encro_snapshot.empty()) {                                         \
      loggerPtr()->log(                                                      \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},             \
        spdlog::level::info,                                                 \
        fmt::format(                                                         \
          "[{}:{}] {}{}",                                                    \
          logging::detail::shortFile(__FILE__),                              \
          __LINE__,                                                          \
          __encro_snapshot,                                                  \
          logging::detail::formatAttributeChain()                            \
        )                                                                    \
      );                                                                     \
    }                                                                        \
  } while (0)

// ── Forward declarations for logging types used by macros ───────────────────

namespace logging {

[[nodiscard]] auto captureEnvironmentSnapshot() -> std::string;

namespace detail {

inline auto formatAttributeChain() -> std::string;

}  // namespace detail

}  // namespace logging

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
    auto const elapsed =  //
      std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_
      )
        .count();
    try {
      LOG_INFO("{} completed in {}ms", stageName_, elapsed);
    } catch (...) {  // NOLINT(bugprone-empty-catch): noexcept dtor must never terminate
    }
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

inline int& truncatedFrameCount() {
  thread_local auto count = int{0};
  return count;
}

inline void pushContextFrame(std::string_view stage, std::string_view detail) {
  auto& stack = contextStack();
  if (stack.size() >= 16) {
    stack.erase(stack.begin());
    ++truncatedFrameCount();
  }
  stack.push_back({stage, detail});
}

inline void popContextFrame() {
  auto& stack = contextStack();
  if (!stack.empty()) { stack.pop_back(); }
}

inline void resetContextStack() {
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

// ── ScopedLogAttributes ──
//
// Parallel to ScopedErrorContext: RAII frames on a thread-local stack,
// serialized at the call site as " [attrs: {json}]" appended after the
// context chain (survives async logging — the formatter re-parses the tail).
// Values are JSON-escaped here; the innermost frame wins for duplicate keys.
// Same 16-frame cap / FIFO eviction as the context stack.

namespace detail {

struct AttributeFrame {
  std::string_view key;
  std::string_view value;
};

inline auto attributeStack() -> std::vector<AttributeFrame>& {
  thread_local auto stack = std::vector<AttributeFrame>{};
  return stack;
}

inline void pushAttributeFrame(std::string_view key, std::string_view value) {
  auto& stack = attributeStack();
  // ponytail: FIFO eviction + pop-by-count can over-pop an outer scope's
  // frames when >16 nested scopes exist (same trade-off as the context
  // stack); real nesting is 2-3 frames, so the cap is unreachable.
  if (stack.size() >= 16) { stack.erase(stack.begin()); }
  stack.push_back({key, value});
}

inline void popAttributeFrame() {
  auto& stack = attributeStack();
  if (!stack.empty()) { stack.pop_back(); }
}

inline void resetAttributeStack() {
  attributeStack().clear();
}

inline auto escapeJsonString(std::string_view text) -> std::string {
  std::string out;
  out.reserve(text.size() + 2);
  out += '"';
  for (auto const ch: text) {
    switch (ch) {
      case '"' : out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          out += std::format("\\u{:04x}", static_cast<unsigned>(ch));
        } else {
          out += ch;
        }
    }
  }
  out += '"';
  return out;
}

inline auto formatAttributeChain() -> std::string {
  auto const& stack = attributeStack();
  if (stack.empty()) { return ""; }

  std::string json = " [attrs: {";
  auto seen = std::vector<std::string_view>{};
  auto first = true;
  // Innermost (top of stack) first; outer frames with a seen key are skipped
  for (auto i = stack.size(); i-- > 0;) {
    auto const& frame = stack[i];
    if (std::ranges::find(seen, frame.key) != seen.end()) { continue; }
    seen.push_back(frame.key);
    if (!first) { json += ','; }
    first = false;
    json += escapeJsonString(frame.key);
    json += ':';
    json += escapeJsonString(frame.value);
  }
  json += "}]";
  return json;
}

}  // namespace detail

class ScopedLogAttributes {
  std::size_t frameCount_{0};
  bool movedFrom_{false};

public:
  ScopedLogAttributes(
    std::initializer_list<std::pair<std::string_view, std::string_view>> attributes
  ) {
    for (auto const& [key, value]: attributes) { detail::pushAttributeFrame(key, value); }
    frameCount_ = attributes.size();
  }

  ~ScopedLogAttributes() noexcept {
    if (movedFrom_) { return; }
    for (auto i = std::size_t{0}; i < frameCount_; ++i) { detail::popAttributeFrame(); }
  }

  ScopedLogAttributes(ScopedLogAttributes const&) = delete;
  auto operator=(ScopedLogAttributes const&) -> ScopedLogAttributes& = delete;

  ScopedLogAttributes(ScopedLogAttributes&& other) noexcept: movedFrom_(false) {
    frameCount_ = other.frameCount_;
    other.movedFrom_ = true;
  }

  auto operator=(ScopedLogAttributes&& other) noexcept -> ScopedLogAttributes& {
    if (this != &other) {
      frameCount_ = other.frameCount_;
      movedFrom_ = false;
      other.movedFrom_ = true;
    }
    return *this;
  }
};

}  // namespace logging
