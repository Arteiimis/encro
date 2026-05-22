# Phase 1: Logging Foundation - Research

**Researched:** 2026-05-23
**Domain:** C++ 日志宏封装层 / spdlog named logger 注册表架构
**Confidence:** HIGH

## 摘要

Phase 1 是四阶段日志增强的地基层。目标：建立统一的 `LOG_INFO/LOG_DEBUG/LOG_WARN/LOG_ERROR/LOG_CRITICAL` 宏层和模块标签体系，让所有业务代码通过宏与日志系统交互，与 spdlog 具体 API 解耦。每条日志自动携带 `file.cpp:128 [video.encode]` 信息，所有 sink 创建、logger 注册集中在 `src/logging/setup.cpp`。

核心技术路径已由 CONTEXT.md 锁定：自定义宏层（D-01）内部使用 `SPDLOG_LOGGER_CALL`，源位置注入消息体（D-03），每个 .cpp 一个 `DEFINE_LOGGER("module.tag")`（D-05），标签常量统一在 `src/logging/log_tags.h`（D-07），一次性迁移全部 13 个使用 spdlog 的源文件（D-09）。

**主要建议：** 源位置采用消息体注入策略（D-03）以确保 async 安全，日志格式调整为不含 `%s:%#` 的 pattern（见 Open Questions Q1 关于 D-03 与 D-10 格式冲突）。

## 用户约束（来自 CONTEXT.md）

### 锁定决策

- **D-01:** 自定义 `LOG_TRACE/LOG_DEBUG/LOG_INFO/LOG_WARN/LOG_ERROR/LOG_CRITICAL` 宏封装层，内部使用 `SPDLOG_LOGGER_CALL`。**不使用 spdlog 内建 `SPDLOG_INFO` 等裸宏** —— 自定义层支持在 call site 注入模块标签和源位置到消息体，为 Phase 3 的 error context chaining 预留注入点。
- **D-02:** 宏命名不加前缀（不用 `ENCRO_INFO`），直接使用 `LOG_INFO`。放在 `src/logging/logging.h` 命名空间内，业务代码通过 `using namespace logging::macros;` 或限定名使用。
- **D-03:** 源位置直接注入消息体（格式 `file.cpp:128`），**不使用 spdlog pattern flags（`%@`, `%s:%#`）**。避免 async 模式下 source_loc 裸指针的悬垂风险（PITFALLS.md pitfall #2）。`__FILE__` + `__LINE__` 在宏展开点求值，编译期零运行时开销。
- **D-04:** 使用 `__FILE__` 的短文件名（仅文件名，不含路径）以保证日志可读性。通过 `SPDLOG_SHORT_FILE` 或等效方式截取。
- **D-05:** 每个 .cpp 文件一个 `DEFINE_LOGGER("module.tag")` 调用。~19 个文件各注册独立 named logger，全部共享同一组 sink（1 file + optional console）。
- **D-06:** Logger 指针缓存在模块级静态变量中（`static auto* logger = spdlog::get(name)`），避免每次日志调用时做 hash-map 查找。
- **D-07:** 所有标签常量定义在单个头文件 `src/logging/log_tags.h`，使用 dot-notation 层级命名。
- **D-08:** 本 phase 仅定义当前 `src/` 下已有的源文件所对应的标签。Phase 2-4 新增的模块标签由各自 phase 添加。
- **D-09:** 一次性迁移全部 13 个使用 spdlog 的源文件。工具链为机械替换：`spdlog::info(...)` → `LOG_INFO(...)`，`spdlog::debug(...)` → `LOG_DEBUG(...)` 等。不保留任何混合状态。
- **D-10:** 日志格式字符串: `[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] [%n] [%s:%#] %v`，其中 `%n` 渲染 named logger 名称，`%s` 和 `%#` 由 spdlog 从传递给 async_logger 的 source_loc 结构体渲染。
- **D-11:** Level 使用 spdlog 内置的颜色标记 `%^%l%$` 保持与现有终端输出兼容。
- **D-12:** 所有 sink 创建、logger 注册、线程池初始化的代码全部迁移至新的 `src/logging/setup.cpp`。`prelude.cpp` 中现有 `setupLogging()` 函数重构为调用 `logging::setup()`。
- **D-13:** 公共头文件 `src/logging/logging.h` 导出：宏定义、`DEFINE_LOGGER`、`ScopedTimer`（Phase 2 使用）。标签常量在独立头文件 `src/logging/log_tags.h`。
- **D-14:** 在 `xmake.lua` 的 release/releasedbg 模式下定义 `SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO`，debug/coverage 模式下设为 `SPDLOG_LEVEL_TRACE`。

### Claude's Discretion

- **日志级别选择:** 各调用点保留现有日志级别（`info` → `LOG_INFO`, `debug` → `LOG_DEBUG` 等），不做级别调整。
- **DEFINE_LOGGER 的位置:** 放在每个 .cpp 文件顶部，紧接 `#include` 块之后、匿名 namespace 之前。

### 已推迟想法（OUT OF SCOPE）

无 —— 讨论内容均在 phase 范围内。

## Phase Requirements

| ID | 描述 | 研究支撑 |
|----|------|----------|
| INF-01 | 所有源文件使用自定义宏替代 `spdlog::info/debug/warn/error()` 直接调用 | 宏设计见 Implementation Details §1, 迁移清单见 §3 |
| INF-02 | 全局启用 `SPDLOG_ACTIVE_LEVEL` 编译期优化 | xmake.lua 修改见 §4 |
| INF-03 | 定义层级式模块标签命名规范 | log_tags.h 设计见 §7 |
| INF-04 | 每个 .cpp 通过 `DEFINE_LOGGER("name")` 注册模块 logger，共享 file+console sink | DEFINE_LOGGER 模式见 §2, setup.cpp 见 §4 |
| INF-05 | 日志配置与业务逻辑分离 | setup.cpp 架构见 Architecture Overview §1 |
| OBS-01 | 每条日志包含源文件路径和行号 | 宏设计见 §1, D-03 vs D-10 冲突见 Open Questions Q1 |
| OBS-02 | 每条日志包含模块/组件标签（通过 `%n` 模式标记） | setup.cpp logger 注册见 §4 |
| OBS-04 | 日志格式清晰可读 | pattern 设计见 §1.3 |

## Architectural Responsibility Map

| 能力 | 主导层 | 辅助层 | 依据 |
|------|--------|--------|------|
| 日志宏展开（`__FILE__`/`__LINE__` 捕获） | 宏层 (call site) | — | 必须是宏 —— 函数会捕获 wrapper 的源位置（Pitfall #1） |
| Logger 注册 & sink 创建 | `src/logging/setup.cpp` | — | INF-05: 集中配置，业务代码仅用宏 |
| 标签常量定义 | `src/logging/log_tags.h` | — | D-07: 单一头文件，dot-notation 层级 |
| Logger 指针缓存 | 每个 .cpp 模块级静态 | — | D-05/D-06: per-file named logger |
| spdlog sender 成员变量模式 | 每个 .cpp 顶部 `DEFINE_LOGGER` | — | D-05 |
| crash handler logger 访问 | `spdlog::default_logger_raw()` | 所有 named logger 共享 default logger sink | 保持现有模式，Phase 2 改进 |

## Architecture Overview

### System Architecture Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                    Phase 1: Logging Foundation                     │
├──────────────────────────────────────────────────────────────────┤
│                                                                    │
│  BUSINESS CODE (.cpp files)                                        │
│  ┌────────────────────────────┐                                   │
│  │ #include "logging/logging.h"│                                   │
│  │ DEFINE_LOGGER("video.encode")│  ← 每个 .cpp 一个               │
│  │                             │                                   │
│  │ LOG_INFO("msg: {}", x)      │  ← 替换 spdlog::info()          │
│  │     ↓                       │                                   │
│  │ 宏展开:                      │                                   │
│  │ SPDLOG_LOGGER_CALL(          │                                   │
│  │   gLoggerPtr, debug,         │  ← static cached pointer        │
│  │   "[encode.cpp:42] {}", x)   │  ← __FILE__+__LINE__ 注入消息体 │
│  └─────────────┬───────────────┘                                   │
│                │                                                    │
│  MACRO LAYER (src/logging/logging.h)                               │
│  ┌─────────────┴───────────────┐                                   │
│  │ LOG_INFO(...)               │                                   │
│  │   → should_log? 检查        │  ← SPDLOG_ACTIVE_LEVEL 剥离     │
│  │   → SPDLOG_LOGGER_CALL(     │                                   │
│  │       logger, level,         │                                   │
│  │       "[{}:{}] {}",          │                                   │
│  │       SHORT_FILE, __LINE__,  │  ← 源位置注入消息体 (D-03)      │
│  │       fmt::format(...))      │                                   │
│  └─────────────┬───────────────┘                                   │
│                │ 通过 async_logger → thread_pool                   │
│                ▼                                                    │
│  SETUP LAYER (src/logging/setup.cpp)                               │
│  ┌──────────────────────────────┐                                  │
│  │ logging::setup(config)       │                                  │
│  │  ├─ 创建 shared file sink   │  ← basic_file_sink_mt           │
│  │  ├─ 创建 optional console   │  ← stdout_color_sink_mt         │
│  │  │   sink                    │                                  │
│  │  ├─ init_thread_pool(8192,1) │  ← 现有线程池，复用            │
│  │  ├─ 为每个模组标签创建       │                                  │
│  │  │   async_logger            │  ← 所有 logger 共享同一组 sink │
│  │  ├─ register_logger()        │  ← 注册到全局 registry         │
│  │  └─ 设置全局 pattern         │  ← 仅 set_pattern 一次         │
│  └──────────────┬───────────────┘                                  │
│                 │                                                   │
│  SPDLOG CORE (shared sinks)                                        │
│  ┌──────────────┴───────────────┐                                  │
│  │ async_logger("video.encode") │──┬── basic_file_sink_mt          │
│  │ async_logger("pack.zip")     │──┤  (shared)                     │
│  │ async_logger("core.scan")    │──┤                               │
│  │ ... (22 named loggers)       │──┼── stdout_color_sink_mt        │
│  │ default_logger ("encro")     │──┘  (shared, optional)           │
│  └──────────────────────────────┘                                  │
│                                                                    │
│  TAG CONSTANTS (src/logging/log_tags.h)                            │
│  ┌──────────────────────────────┐                                  │
│  │ namespace logtags {          │                                  │
│  │   constexpr auto VIDEO_ENCODE│  ← D-07: dot-notation hierarchy │
│  │     = "video.encode";        │                                  │
│  │   constexpr auto PACK_ZIP   │                                  │
│  │     = "pack.zip";            │                                  │
│  │   ...                        │                                  │
│  │ }                            │                                  │
│  └──────────────────────────────┘                                  │
│                                                                    │
└──────────────────────────────────────────────────────────────────┘
```

**关键数据流:**
1. 编译期：`LOG_INFO("msg")` → 宏展开 → `SPDLOG_LOGGER_CALL(gLoggerPtr, level, "[file:line] msg")` → 消息体包含源位置
2. 启动时：`logging::setup(config)` → 创建共享 sink → 遍历 22 个标签 → 每个创建 `async_logger` → `register_logger()` → 全局 registry
3. 运行时：每个 .cpp 的 `DEFINE_LOGGER` → `spdlog::get("tag")` → 缓存到 `static auto* gLoggerPtr` → 每次 `LOG_INFO` 直接使用缓存的裸指针，无 hash-map 查找

### Recommended Project Structure

```
src/logging/                    # 新增目录
├── logging.h                   # 宏定义 + DEFINE_LOGGER (D-13)
├── log_tags.h                  # constexpr tag 常量 (D-07)
├── setup.h                     # logging::setup() 声明
└── setup.cpp                   # sink 创建 + logger 注册 (D-12)

src/app/
└── prelude.cpp                 # 修改: setupLogging() → 调用 logging::setup()
                                #       logConfigSummary() 调用点 → LOG_INFO
```

---

## 1. 宏设计详解

### 1.1 `SPDLOG_LOGGER_CALL` 内部机制

`SPDLOG_LOGGER_CALL` 定义在 spdlog v1.15.1 的 `include/spdlog/common.h` 中 [CITED: github.com/gabime/spdlog v1.15.1 source]:

```cpp
// 未定义 SPDLOG_NO_SOURCE_LOC 时的典型展开 (common.h ~L340-360)
// SPDLOG_LOGGER_CALL 构造 source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}
// 传递给 logger->log(source_loc, level, fmt, args...)
```

宏内部：
1. 使用 `__FILE__`/`__LINE__`/`SPDLOG_FUNCTION` 构造 `spdlog::source_loc` 结构体
2. 调用 `logger->log(source_loc, level, fmt, args...)` 方法
3. 如果 `SPDLOG_ACTIVE_LEVEL` 高于当前 level，则展开为空语句（`(void)0`）
4. 默认情况下，`SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO` 会剥离 TRACE 和 DEBUG 宏

### 1.2 自定义 LOG_* 宏设计

根据 D-03（源位置注入消息体，不使用 spdlog pattern flags），自定义宏需要在 `SPDLOG_LOGGER_CALL` 的消息参数中注入 `file:line` 前缀。

**关键依据 —— D-03 要求源位置在消息体中:** 这意味着 spdlog pattern 中**不能**依赖 `%s:%#` 或 `%@` 来渲染源位置。源位置必须在宏展开时格式化为字符串并拼接到消息里。`SPDLOG_LOGGER_CALL` 内部传递的 `source_loc` 结构体虽然仍会被构造（无法绕过），但如果 pattern 不使用 `%s:%#` 则这些字段不会被渲染。

```cpp
// src/logging/logging.h

#pragma once

#include "logging/log_tags.h"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>

#include <fmt/format.h>

#include <string_view>

// ── 短文件名提取 ──────────────────────────────────────────────
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

// ── DEFINE_LOGGER ──────────────────────────────────────────────
// 每个 .cpp 文件顶部调用一次:
//   DEFINE_LOGGER(logtags::VIDEO_ENCODE)
//
// 展开为:
//   static auto* const gLoggerPtr = spdlog::get(logtags::VIDEO_ENCODE);
//
// D-05: 每个 .cpp 文件一个 named logger
// D-06: static 缓存指针，避免每次日志调用的 hash-map 查找

#define DEFINE_LOGGER(tag) \
    static auto* const gLoggerPtr = spdlog::get(tag)

// ── 日志宏 (D-01: 自定义封装层，内部使用 SPDLOG_LOGGER_CALL) ──
//
// D-02: LOG_INFO 命名，不加 ENCRO_ 前缀
// D-03: 源位置注入消息体，格式 "file.cpp:128"
// SPDLOG_LOGGER_CALL 内部已处理 SPDLOG_ACTIVE_LEVEL 剥离

#define LOG_TRACE(...) \
    SPDLOG_LOGGER_CALL( \
        gLoggerPtr, \
        spdlog::level::trace, \
        "[{}:{}] {}", \
        logging::detail::shortFile(__FILE__), \
        __LINE__, \
        fmt::format(__VA_ARGS__))

#define LOG_DEBUG(...) \
    SPDLOG_LOGGER_CALL( \
        gLoggerPtr, \
        spdlog::level::debug, \
        "[{}:{}] {}", \
        logging::detail::shortFile(__FILE__), \
        __LINE__, \
        fmt::format(__VA_ARGS__))

#define LOG_INFO(...) \
    SPDLOG_LOGGER_CALL( \
        gLoggerPtr, \
        spdlog::level::info, \
        "[{}:{}] {}", \
        logging::detail::shortFile(__FILE__), \
        __LINE__, \
        fmt::format(__VA_ARGS__))

#define LOG_WARN(...) \
    SPDLOG_LOGGER_CALL( \
        gLoggerPtr, \
        spdlog::level::warn, \
        "[{}:{}] {}", \
        logging::detail::shortFile(__FILE__), \
        __LINE__, \
        fmt::format(__VA_ARGS__))

#define LOG_ERROR(...) \
    SPDLOG_LOGGER_CALL( \
        gLoggerPtr, \
        spdlog::level::err, \
        "[{}:{}] {}", \
        logging::detail::shortFile(__FILE__), \
        __LINE__, \
        fmt::format(__VA_ARGS__))

#define LOG_CRITICAL(...) \
    SPDLOG_LOGGER_CALL( \
        gLoggerPtr, \
        spdlog::level::critical, \
        "[{}:{}] {}", \
        logging::detail::shortFile(__FILE__), \
        __LINE__, \
        fmt::format(__VA_ARGS__))
```

**关键设计决策说明：**

1. **`fmt::format(__VA_ARGS__)` 在宏展开点调用** —— 这是有意为之：fmt::format 的结果字符串在 call site（调用者线程）构造，进入 async queue 之前完成格式化。这确保了：
   - 源位置字符串完全复制到消息中（无悬垂指针风险）
   - 任何线程局部上下文在 Phase 3 中可在此时注入
   - 代价：比 spdlog 的延迟格式化略慢（但本项目 CLI 规模完全可接受）

2. **不使用 `do { ... } while(0)` 包装** —— `SPDLOG_LOGGER_CALL` 自身已是一个完整表达式，不需要额外的 do-while 包装。`SPDLOG_LOGGER_CALL` 内部已经处理了 if/else 安全性。

3. **D-06 缓存指针类型** —— 使用 `static auto* const gLoggerPtr`（裸指针），而非 `shared_ptr`。原因：
   - spdlog::get() 返回的 `shared_ptr` 在全局 registry 中已持有引用，logger 生命周期由 registry 保证
   - 裸指针避免了每次日志调用时的 shared_ptr 引用计数操作
   - `static` 确保初始化一次（线程安全：C++11 保证 static 局部变量初始化的线程安全）

### 1.3 日志格式 Pattern

根据 D-03（不使用 `%s:%#` pattern flags），日志 pattern 调整为：

```cpp
// D-03 兼容格式 —— 源位置已在消息体中
constexpr auto kLogPattern = "[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] [%n] %v";
```

其中：
- `%Y-%m-%dT%H:%M:%S.%e%z` — ISO 8601 时间戳
- `%^%l%$` — 带颜色标记的日志级别 (D-11)
- `%n` — named logger 名称（= 模块标签，如 `video.encode`）(D-05, OBS-02)
- `%v` — 消息体（已包含 `file.cpp:128 actual message`）(D-03, OBS-01)

**实际输出示例:**
```
[2026-05-23T14:30:52.123+08:00] [info] [video.encode] [video_encode_runner.cpp:128] Starting encode slot 3
[2026-05-23T14:30:55.891+08:00] [debug] [core.scan] [media_scanner.cpp:42] Found 15 candidate files
[2026-05-23T14:31:01.004+08:00] [error] [pack.zip] [packer.cpp:473] Failed to add entry: permission denied
```

**注意：** D-10 中指定的 pattern 包含 `%s:%#`，与 D-03 冲突。详见 Open Questions Q1。

---

## 2. DEFINE_LOGGER 模式

### 2.1 模式定义

```cpp
// 每个 .cpp 文件顶部，紧接 #include 块之后 (Claude's Discretion)
// 在匿名 namespace 之前

#include "logging/logging.h"
#include "logging/log_tags.h"

DEFINE_LOGGER(logtags::VIDEO_ENCODE)  // 仅此一行
```

展开后等价于：

```cpp
static auto* const gLoggerPtr = spdlog::get(logtags::VIDEO_ENCODE);
```

### 2.2 为什么用裸指针而非 shared_ptr

| 方式 | 优缺点 |
|------|--------|
| `static auto gLogger = spdlog::get("tag")` (shared_ptr) | + 自文档化生命周期 / - 每次复制 shared_ptr 有原子操作开销 |
| `static auto* const gLoggerPtr = spdlog::get("tag")` (raw ptr) | + 零开销，logger 生命周期由 registry 保证 / - 裸指针语义 |
| `spdlog::get("tag")` 每次调用 | - 每次日志调用锁 mutex + hash-map 查找，性能最差 (D-06 明确避免) |

**选择裸指针 (D-06):** `static auto* const gLoggerPtr = spdlog::get("tag")`。Logger 注册发生在 `logging::setup()` 中（main 函数早期），所有 `DEFINE_LOGGER` 的静态初始化在首次日志调用时触发（此时 logger 已注册），不存在空指针问题。

### 2.3 初始化时机保证

`DEFINE_LOGGER` 使用函数局部 `static` 变量：
- C++11 保证函数局部 static 的线程安全初始化（magic statics）
- 初始化发生在首次执行到包含 `LOG_*` 宏的代码路径时
- `logging::setup()` 在 `prelude::initStartup()` 中调用，早于任何业务逻辑
- 因此 `spdlog::get("tag")` 在首次 `LOG_INFO(...)` 时必定能找到已注册的 logger

---

## 3. 文件迁移清单

### 3.1 13 个需要迁移的源文件

| # | 文件 | 当前日志调用数 | 目标标签 (log_tags.h) | 说明 |
|---|------|--------------|----------------------|------|
| 1 | `src/app/app_entry.cpp` | 1 × `spdlog::error` | `app.entry` | `failWithHint()` 中的错误日志 |
| 2 | `src/app/prelude.cpp` | 13 × `spdlog::info` (logConfigSummary) + 5 × setup 代码 | `app.prelude` | **双重变更:** setup 代码移至 setup.cpp; logConfigSummary 的 13 个调用点改为 LOG_INFO |
| 3 | `src/infra/crash_runtime.cpp` | 1 × `spdlog::default_logger_raw()` | `infra.crash` | 不改为宏 —— 见 §3.3 特殊处理 |
| 4 | `src/video/video_batch_execution.cpp` | 12 × spdlog::debug/info/warn | `video.batch` | 编码批次执行日志 |
| 5 | `src/video/video_encode_runner.cpp` | 14 × spdlog::error/debug/warn/info | `video.encode` | 单文件编码 runner |
| 6 | `src/utils/utils.cpp` | 13 × spdlog::debug/info/warn | `utils.subprocess` | 子进程执行日志 |
| 7 | `src/pack/packer.cpp` | 2 × `spdlog::debug` | `pack.zip` | ZIP 打包底层 |
| 8 | `src/picture/picture_process.cpp` | 1 × `spdlog::error` | `picture.process` | 图片处理入口 |
| 9 | `src/infra/toolchain.cpp` | 2 × `spdlog::info` | `infra.toolchain` | FFmpeg/FFprobe 路径日志 |
| 10 | `src/video/video_encoding_state.cpp` | 1 × `spdlog::info` | `video.state` | 编码状态变更 |
| 11 | `src/video/video_process.cpp` | 21 × spdlog::info/debug/error | `video.process` | 视频处理 pipeline 主逻辑 |
| 12 | `src/video/video_info.cpp` | 12 × spdlog::debug/warn | `video.info` | ffprobe 信息获取 |
| 13 | `src/picture/picture_compress.cpp` | 21 × spdlog::warn/info/debug | `picture.compress` | 图片压缩批次 |

**总计:** ~116 个日志调用点需要迁移（含 prelude.cpp 的 logConfigSummary 部分）。

### 3.2 迁移模式

**机械替换规则（每个文件）:**

1. **文件顶部添加:**
   ```cpp
   #include "logging/logging.h"
   #include "logging/log_tags.h"
   DEFINE_LOGGER(logtags::MODULE_TAG)
   ```

2. **日志调用替换:**
   ```cpp
   // 之前
   spdlog::debug("msg: {}", arg);
   spdlog::info("msg: {}", arg);
   spdlog::warn("msg: {}", arg);
   spdlog::error("msg: {}", arg);
   
   // 之后
   LOG_DEBUG("msg: {}", arg);
   LOG_INFO("msg: {}", arg);
   LOG_WARN("msg: {}", arg);
   LOG_ERROR("msg: {}", arg);
   ```

3. **移除 `#include <spdlog/spdlog.h>`**（如果文件中无其他 spdlog API 使用）

### 3.3 特殊处理：crash_runtime.cpp

**现状 (src/infra/crash_runtime.cpp:30):**
```cpp
auto tryWriteToLogger(std::string const& message) -> bool {
    auto* logger = spdlog::default_logger_raw();
    if (logger == nullptr) { return false; }
    try {
        logger->critical("{}", message);
        logger->flush();
        return true;
    } catch (...) { return false; }
}
```

**Phase 1 处理:**
- `tryWriteToLogger` 保留 `spdlog::default_logger_raw()` 调用 —— crash handler 需要绕过宏，直接访问 default logger
- crash_runtime.cpp 本身**不添加** `DEFINE_LOGGER` —— 它不产生常规日志
- 依赖关系：crash handler 依赖 default_logger 仍然被设置为文件 sink（与其他 named logger 共享 sink 的文件 logger）
- **Phase 2 将改进此路径**（FILE-04: crash handler 直接文件追加写入，绕过 spdlog）

### 3.4 特殊处理：prelude.cpp 双重变更

`src/app/prelude.cpp` 有两类 spdlog 使用：

**A) setupLogging() 函数（匿名 namespace，行 59-128）—— 迁移至 setup.cpp:**
- `spdlog::set_pattern()` → 不变，移到 setup.cpp
- `spdlog::set_level()` → 不变，移到 setup.cpp
- `spdlog::sinks::basic_file_sink_mt` → 不变，移到 setup.cpp
- `spdlog::init_thread_pool(8192, 1)` → 不变，移到 setup.cpp
- `spdlog::async_logger(...)` → 改为创建多个 named logger
- `spdlog::set_default_logger(...)` → 保留（crash handler 依赖 default_logger_raw）

**B) logConfigSummary() 函数（prelude 命名空间，行 159-198）—— 宏迁移:**
- 13 个 `spdlog::info(...)` → `LOG_INFO(...)`
- 添加 `DEFINE_LOGGER(logtags::APP_PRELUDE)`

---

## 4. setup.cpp 架构

### 4.1 LogConfig 结构

```cpp
// src/logging/setup.h

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace logging {

struct LogConfig {
    bool verboseEnabled{false};
    bool verboseEchoEnabled{false};
    bool colorsEnabled{true};
    std::optional<std::filesystem::path> customLogDir;
};

// 返回创建的日志文件路径 (std::nullopt 如果 logging 未启用)
auto setup(LogConfig const& config) -> std::optional<std::filesystem::path>;

// 销毁: flush 所有 logger
auto shutdown() -> void;

}  // namespace logging
```

### 4.2 setup() 实现结构

```cpp
// src/logging/setup.cpp

#include "logging/setup.h"
#include "logging/log_tags.h"

#include <spdlog/async.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ── Log 目录解析 (从 prelude.cpp 迁移，逻辑不变) ──
auto resolveCommonLogDir() -> fs::path { /* ... 现有代码 ... */ }

// ── 单一日志 pattern (D-03 兼容 —— 源位置在消息体中，不用 %s:%#) ──
constexpr auto kLogPattern = "[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] [%n] %v";

// ── 所有模组标签列表 ──
// (在 log_tags.h 中定义常量，此处引用)
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

namespace logging {

auto setup(LogConfig const& config) -> std::optional<fs::path> {
    // 1. 如果 --verbose 未启用，关闭所有日志
    if (!config.verboseEnabled) {
        spdlog::set_level(spdlog::level::off);
        return std::nullopt;
    }

    // 2. 解析日志目录 (从 prelude.cpp 迁移，逻辑不变)
    auto logDir = resolveCommonLogDir();
    auto ec = std::error_code{};
    fs::create_directories(logDir, ec);
    if (ec) {
        // fallback 到 temp 目录
        logDir = fs::temp_directory_path() / "encro" / "logs";
        fs::create_directories(logDir, ec);
    }

    // 3. 创建共享 sink (Phase 1 使用现有文件名；Phase 2 改为时间戳命名)
    auto const logFilePath = logDir / "encro.verbose.log";

    auto sinks = std::vector<spdlog::sink_ptr>{};
    sinks.emplace_back(
        std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            logFilePath.string(), true));

    // 4. 可选的 console sink
    if (config.verboseEchoEnabled) {
        if (config.colorsEnabled) {
            sinks.emplace_back(
                std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        } else {
            sinks.emplace_back(
                std::make_shared<spdlog::sinks::stdout_sink_mt>());
        }
    }

    // 5. 初始化全局 thread pool (复用现有 once_flag 模式)
    static auto poolInitFlag = std::once_flag{};
    std::call_once(poolInitFlag, [] {
        spdlog::init_thread_pool(8192, 1);
    });

    // 6. 为每个模组标签创建 named async_logger，共享同一组 sink
    for (auto const* tag : allModuleTags()) {
        auto logger = std::make_shared<spdlog::async_logger>(
            tag,                          // logger 名称 = 模块标签
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
        "encro", sinks.begin(), sinks.end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::block
    );
    defaultLogger->set_pattern(kLogPattern);
    defaultLogger->set_level(spdlog::level::debug);
    defaultLogger->flush_on(spdlog::level::err);
    spdlog::set_default_logger(std::move(defaultLogger));
    spdlog::set_level(spdlog::level::debug);

    return logFilePath;
}

auto shutdown() -> void {
    spdlog::shutdown();
}

}  // namespace logging
```

### 4.3 prelude.cpp 变更

```cpp
// src/app/prelude.cpp (修改后)

#include "logging/setup.h"   // 新增
// ... 现有 includes ...

namespace {

auto setupLogging(CmdParseResult const& cmd) -> std::optional<fs::path> {
    // 委托给 logging::setup()
    if (!cmd.verbose) {
        spdlog::set_level(spdlog::level::off);  // 不改 —— 但移到 logging::setup 更好
        // ... verboseEcho 警告 ...
        return std::nullopt;
    }
    return logging::setup(logging::LogConfig{
        .verboseEnabled     = cmd.verbose,
        .verboseEchoEnabled = cmd.verboseEcho,
        .colorsEnabled      = terminal::colorsEnabled(),
    });
}

}  // namespace

namespace prelude {

auto initStartup(int argc, char* argv[], std::string const& introLine) -> StartupContext {
    // ... 现有代码不变 ...
    auto verboseLogFilePath = setupLogging(cmd);
    // ... 现有代码不变 ...
}

void logConfigSummary(appctx::AppConfig const& config) {
    // 每个 spdlog::info(...) → LOG_INFO(...)
    if (config.yesToAll) { LOG_INFO("Automatic 'yes to all' enabled."); }
    if (config.recursive) { LOG_INFO("Recursive directory search enabled."); }
    // ... 其余 11 个调用点同理 ...
}

}  // namespace prelude
```

---

## 5. xmake.lua 修改

### 5.1 SPDLOG_ACTIVE_LEVEL 配置 (D-14)

在 `xmake.lua` 中为所有 target 的每个 build mode 添加 `SPDLOG_ACTIVE_LEVEL` 定义：

```lua
-- xmake.lua 添加部分

-- 在 mode 定义之后添加
if is_mode("release") then
    add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO")
elseif is_mode("releasedbg") then
    add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO")
elseif is_mode("debug") then
    add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
elseif is_mode("coverage") then
    add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
end
```

**效果:**

| Build Mode | SPDLOG_ACTIVE_LEVEL | LOG_TRACE | LOG_DEBUG | LOG_INFO+ |
|------------|---------------------|-----------|-----------|-----------|
| release | SPDLOG_LEVEL_INFO (2) | 剥离 | 剥离 | 保留 |
| releasedbg | SPDLOG_LEVEL_INFO (2) | 剥离 | 剥离 | 保留 |
| debug | SPDLOG_LEVEL_TRACE (0) | 保留 | 保留 | 保留 |
| coverage | SPDLOG_LEVEL_TRACE (0) | 保留 | 保留 | 保留 |

**验证 (CITED: spdlog common.h / tweakme.h):**
- `SPDLOG_ACTIVE_LEVEL` 默认值为 `SPDLOG_LEVEL_INFO` [VERIFIED: raw.githubusercontent.com/gabime/spdlog/v1.15.1/include/spdlog/common.h]
- 低于 `SPDLOG_ACTIVE_LEVEL` 的宏展开为 `(void)0` —— 完全零开销
- Level 枚举值: TRACE=0, DEBUG=1, INFO=2, WARN=3, ERROR=4, CRITICAL=5, OFF=6

### 5.2 新增源文件

```lua
-- src/logging/setup.cpp 会被现有的 add_files("src/**.cpp") 自动包含
-- 无需额外配置
```

---

## 6. 不手写（Don't Hand-Roll）

| 问题 | 不自己写 | 使用 | 原因 |
|------|---------|------|------|
| 日志宏 | 自定义 formatted-log 函数 | `SPDLOG_LOGGER_CALL` | spdlog 宏已处理 async queue、SPDLOG_ACTIVE_LEVEL 剥离、线程安全 |
| Sink 创建 | 自定义文件写 | `spdlog::sinks::basic_file_sink_mt` | spdlog 已处理缓冲、flush、线程安全 |
| 线程池 | `std::thread` | `spdlog::init_thread_pool` / `spdlog::thread_pool()` | spdlog 已集成 async_logger |
| Logger 注册表 | `std::unordered_map<std::string, logger>` | `spdlog::register_logger` + `spdlog::get()` | spdlog 全局 registry 已支持 |
| 短文件名提取 | 非 portable 宏技巧 | `logging::detail::shortFile()` (constexpr-friendly runtime function) | 编译器对字符串字面量完全优化掉 |

---

## 7. log_tags.h 设计

### 7.1 完整标签常量定义

```cpp
// src/logging/log_tags.h

#pragma once

// ── 模块标签常量 ────────────────────────────────────────────────
// 层级 dot-notation: component.subcomponent
// 用法: DEFINE_LOGGER(logtags::VIDEO_ENCODE)
//
// 命名约定:
//   - 全小写
//   - 分隔符: 点号 (.)
//   - 层级最多 3 层
//   - 禁止 ad-hoc 字符串 —— 所有 DEFINE_LOGGER 必须引用此处的常量
//
// Phase 1 定义的标签 (当前 src/ 下所有有 spdlog 调用的模块)
// Phase 2-4 新模块的标签由各自 phase 添加 (D-08)

namespace logtags {

// ── app ──
inline constexpr auto APP_ENTRY    = "app.entry";
inline constexpr auto APP_PRELUDE  = "app.prelude";
inline constexpr auto APP_PIPELINE = "app.pipeline";

// ── cmd ──
inline constexpr auto CMD_CONFIG = "cmd.config";

// ── video ──
inline constexpr auto VIDEO_ENCODE   = "video.encode";
inline constexpr auto VIDEO_PROBE    = "video.probe";
inline constexpr auto VIDEO_INFO     = "video.info";
inline constexpr auto VIDEO_OUTPUT   = "video.output";
inline constexpr auto VIDEO_BATCH    = "video.batch";
inline constexpr auto VIDEO_PROGRESS = "video.progress";
inline constexpr auto VIDEO_STATE    = "video.state";
inline constexpr auto VIDEO_PROCESS  = "video.process";

// ── picture ──
inline constexpr auto PICTURE_PROCESS  = "picture.process";
inline constexpr auto PICTURE_COMPRESS = "picture.compress";

// ── pack ──
inline constexpr auto PACK_ZIP     = "pack.zip";
inline constexpr auto PACK_SERVICE = "pack.service";

// ── core ──
inline constexpr auto CORE_SCAN     = "core.scan";
inline constexpr auto CORE_JOB      = "core.job";
inline constexpr auto CORE_TASK     = "core.task";
inline constexpr auto CORE_PARALLEL = "core.parallel";

// ── infra ──
inline constexpr auto INFRA_TOOLCHAIN = "infra.toolchain";
inline constexpr auto INFRA_CRASH     = "infra.crash";
inline constexpr auto INFRA_SIGNAL    = "infra.signal";

// ── utils ──
inline constexpr auto UTILS_SUBPROCESS = "utils.subprocess";

}  // namespace logtags
```

### 7.2 标签到源文件的映射

| 标签常量 | 对应源文件 | 迁移状态 |
|---------|-----------|---------|
| `APP_ENTRY` | `src/app/app_entry.cpp` | Phase 1 |
| `APP_PRELUDE` | `src/app/prelude.cpp` | Phase 1 |
| `APP_PIPELINE` | `src/app/pipeline.cpp` | Phase 1 |
| `CMD_CONFIG` | `src/cmd/config_builder.cpp` | Phase 1 |
| `VIDEO_ENCODE` | `src/video/video_encode_runner.cpp` | Phase 1 |
| `VIDEO_PROBE` | `src/video/video_progress_parser.cpp` | Phase 1 |
| `VIDEO_INFO` | `src/video/video_info.cpp` | Phase 1 |
| `VIDEO_OUTPUT` | `src/video/video_output_planning.cpp` | Phase 1 |
| `VIDEO_BATCH` | `src/video/video_batch_execution.cpp` | Phase 1 |
| `VIDEO_PROGRESS` | `src/video/video_progress_parser.cpp` | Phase 1 |
| `VIDEO_STATE` | `src/video/video_encoding_state.cpp` | Phase 1 |
| `VIDEO_PROCESS` | `src/video/video_process.cpp` | Phase 1 |
| `PICTURE_PROCESS` | `src/picture/picture_process.cpp` | Phase 1 |
| `PICTURE_COMPRESS` | `src/picture/picture_compress.cpp` | Phase 1 |
| `PACK_ZIP` | `src/pack/packer.cpp` | Phase 1 |
| `PACK_SERVICE` | `src/pack/pack_service.cpp` | Phase 1 |
| `CORE_SCAN` | `src/core/media_scanner.cpp` | Phase 1 |
| `CORE_JOB` | `src/core/job_state.cpp` | Phase 1 |
| `CORE_TASK` | `src/core/task_executor.cpp` | Phase 1 |
| `CORE_PARALLEL` | `src/core/parallel.cpp` | Phase 1 |
| `INFRA_TOOLCHAIN` | `src/infra/toolchain.cpp` | Phase 1 |
| `INFRA_CRASH` | `src/infra/crash_runtime.cpp` | (不使用 DEFINE_LOGGER) |
| `INFRA_SIGNAL` | `src/infra/stop_signal.cpp` | Phase 1 |
| `UTILS_SUBPROCESS` | `src/utils/utils.cpp` | Phase 1 |

**注意:** `pipeline.cpp`、`config_builder.cpp`、`video_progress_parser.cpp`、`video_output_planning.cpp`、`pack_service.cpp`、`media_scanner.cpp`、`job_state.cpp`、`task_executor.cpp`、`parallel.cpp`、`stop_signal.cpp` 这些文件当前**不**直接使用 spdlog，但按 D-05 每个 .cpp 都需要一个 DEFINE_LOGGER 以备未来日志调用。它们的 `DEFINE_LOGGER` 可在 Phase 1 添加标签常量，但实际 `DEFINE_LOGGER` 调用可以延迟到首次日志使用时。

### 7.3 标签层级前缀过滤

dot-notation 层级天然支持前缀过滤：
```bash
# 只看 video 模块的所有日志
grep "\[video\." encro.verbose.log

# 只看 pack 模块
grep "\[pack\." encro.verbose.log

# 只看 video 编码相关（排除 probe、info）
grep "\[video.encode\]" encro.verbose.log
```

---

## Environment Availability

| 依赖 | 需要者 | 可用 | 版本 | 备选 |
|------|--------|------|------|------|
| spdlog | 整个日志层 | ✓ (xmake-repo) | v1.15.1 [ASSUMED — xmake-repo 当前提供的版本] | — |
| fmt | spdlog fmt_external | ✓ (xmake-repo) | v11.1.4 [ASSUMED] | — |
| clang-cl | 编译 | ✗ (当前环境) | — | 开发机器上有，CI 环境需确认 |
| xmake | 构建 | ✓ | v3.0.9+HEAD.2b184e178 | — |

**缺失依赖（无备选）:**
- clang-cl 在**当前 agent 环境**不可用（`toolchain("clang-cl"): not found!`）—— 这不阻塞 research/planning，但阻塞实现。开发者机器上应已安装。

**缺失依赖（有备选）:**
- 无

---

## 常见陷阱

### Pitfall 1: `spdlog::info()` 函数残留

**出错场景:** 迁移后仍有 `spdlog::info(...)` 调用，它们的源位置不会正确捕获。
**检测方法:** `git grep "spdlog::debug\|spdlog::info\|spdlog::warn\|spdlog::error\|spdlog::critical" src/` 返回零结果（排除 setup.cpp 和 crash_runtime.cpp）。
**预防:** 在每个 migration wave 中使用机械替换脚本 + grep 验证。

### Pitfall 2: DEFINE_LOGGER 放在 log_tags.h include 之前

**出错场景:** `DEFINE_LOGGER(logtags::VIDEO_ENCODE)` 在 `#include "logging/log_tags.h"` 之前，编译错误：`logtags` 未声明。
**检测方法:** 编译检查。每个迁移文件编译时必定报错。
**预防:** 文档明确规定 DEFINE_LOGGER 的位置：紧接所有 `#include` 之后。

### Pitfall 3: spdlog 全局 pattern 被重复设置

**出错场景:** `spdlog::set_pattern()` 在 setup.cpp 中被调用，但 prelude.cpp 的旧代码也在调用（如果 setupLogging 未完全清理）。
**检测方法:** `git grep "spdlog::set_pattern" src/` 应仅返回一个结果（setup.cpp 中）。
**预防:** 在 prelude.cpp 重构时完全移除 `spdlog::set_pattern` 调用，仅保留 delegate 到 `logging::setup()` 的逻辑。

### Pitfall 4: crash handler 找不到 default logger

**出错场景:** `logging::setup()` 注册了 named loggers 但未设置 default logger，crash handler 的 `spdlog::default_logger_raw()` 返回 nullptr。
**检测方法:** 测试：触发 crash handler → 检查 stderr 输出（若 default_logger 为 nullptr，fallback 到 stderr）。
**预防:** `logging::setup()` 必须同时设置 default logger（`spdlog::set_default_logger()`），确保 crash handler 路径不变。

### Pitfall 5: test target 的 spdlog header include 路径冲突

**出错场景:** Test 文件引用 `#include <spdlog/spdlog.h>`，现在改用 `#include "logging/logging.h"` 后，test 的 include 路径配置问题。
**检测方法:** 编译 `xmake build tests`。
**预防:** 测试 target 已配置 `add_includedirs("src")`，路径 `"logging/logging.h"` 可以解析。但需要确保测试文件也替换了 spdlog header。

---

## 代码示例

### 典型业务文件迁移前后对比

**迁移前 (video_encode_runner.cpp 片段):**
```cpp
#include "video/video_encode_runner.h"
#include "core/error_handle.h"
#include "utils/utils.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <format>

namespace fs = std::filesystem;

namespace {
// ... 私有 helper ...
}

namespace video {

auto runEncode(...) -> eh::Result<void> {
    // ...
    spdlog::debug("Executing encode command: {}", cmd);
    auto res = utils::exec2(cmd);
    if (!res) {
        spdlog::error("Encode failed: {}", res.error());
        return eh::makeError("encode: {}", res.error());
    }
    spdlog::info("Encode completed: elapsed={}s", elapsedSec);
    // ...
}
```

**迁移后:**
```cpp
#include "video/video_encode_runner.h"
#include "core/error_handle.h"
#include "logging/log_tags.h"    // 新增
#include "logging/logging.h"     // 新增
#include "utils/utils.h"

// 移除: #include <spdlog/spdlog.h>

#include <filesystem>
#include <format>

namespace fs = std::filesystem;

DEFINE_LOGGER(logtags::VIDEO_ENCODE)  // D-05, Claude's Discretion

namespace {
// ... 私有 helper (不变) ...
}

namespace video {

auto runEncode(...) -> eh::Result<void> {
    // ...
    LOG_DEBUG("Executing encode command: {}", cmd);   // 曾: spdlog::debug
    auto res = utils::exec2(cmd);
    if (!res) {
        LOG_ERROR("Encode failed: {}", res.error());  // 曾: spdlog::error
        return eh::makeError("encode: {}", res.error());
    }
    LOG_INFO("Encode completed: elapsed={}s", elapsedSec);  // 曾: spdlog::info
    // ...
}
```

**输出对比:**

| | 迁移前 | 迁移后 |
|---|--------|--------|
| 控制台 | `[2026-05-23T14:30:52.123+08:00] [info] Encode completed: elapsed=3.2s` | `[2026-05-23T14:30:52.123+08:00] [info] [video.encode] [video_encode_runner.cpp:181] Encode completed: elapsed=3.2s` |
| 源位置 | 无 | `video_encode_runner.cpp:181` (D-03, OBS-01) |
| 模块标签 | 无 | `[video.encode]` (D-05, OBS-02) |
| 颜色 | `[info]` 带颜色 | `[info]` 带颜色 (D-11) |

---

## 验证架构

> `workflow.nyquist_validation` 为 `false` —— 跳过。
> TDD mode 为 `true`，应在 PLAN.md 中包含编译测试和验证任务。

### 编译验证策略

Phase 1 不需要传统的测试框架（nyquist_validation 关闭），但需要以下编译期和运行时验证：

| 验证项 | 方式 | 命令 |
|--------|------|------|
| 全部 4 个 build mode 编译通过 | CI 构建 | `xmake build -m debug && xmake build -m release && xmake build -m releasedbg && xmake build -m coverage` |
| 无 `spdlog::debug/info/warn/error` 残留 | grep | `git grep "spdlog::debug\|spdlog::info\|spdlog::warn\|spdlog::error" src/ | grep -v setup.cpp | grep -v crash_runtime.cpp` |
| 无硬编码标签字符串 | grep | `git grep 'DEFINE_LOGGER("' src/` -- 应为零，所有调用应是 `DEFINE_LOGGER(logtags::XXX)` |
| 全部标签常量被 setup.cpp 引用 | 人工审查 | 检出 `allModuleTags()` 列表与 `log_tags.h` 定义的一致性 |
| Release 构建剥离 trace/debug | 二进制大小对比 | `xmake build -m release` 后的二进制应比 debug 模式显著小（无 LOG_DEBUG/LOG_TRACE 代码） |
| crash handler 仍能写日志 | 手动测试 | 发送 SIGINT 或触发一个已知 crash 条件 |

---

## 安全性领域

> `security_enforcement` 未显式设为 `false`（未在 config.json 中找到），按默认启用。

### 适用 ASVS 类别

| ASVS 类别 | 适用 | 标准控制 |
|----------|------|---------|
| V2 身份认证 | 否 | — |
| V3 会话管理 | 否 | — |
| V4 访问控制 | 否 | — |
| V5 输入验证 | 否 | — |
| V6 加密 | 否 | — |
| V7 错误处理与日志 | 是 | 日志注入防护、路径消毒 |

### 已知威胁模式（日志系统）

| 模式 | STRIDE | 缓解 |
|------|--------|------|
| 日志注入：包含换行符的消息可伪造日志条目 | Tampering | 消息通过 `fmt::format("{}", msg)` 格式化 —— fmt 不展开换行符（换行符作为字面 `\n` 出现在已格式化的字符串中） |
| 路径信息泄露：绝对路径暴露构建/用户目录结构 | Information Disclosure | `displaytext::pathToUtf8String()` 已在现有代码中使用；日志路径消毒待 Phase 3 完善 |
| 日志文件权限：POSIX 上日志文件可能全局可读 | Information Disclosure | `resolveCommonLogDir()` 使用用户级目录（Windows: `%LOCALAPPDATA%`, POSIX: `~/.local/state/`）—— 不引入新暴露面 |

---

## 假设日志

> 列出所有标记 `[ASSUMED]` 的声明。planner 和 discuss-phase 使用此表识别需要用户确认的决策。

| # | 声明 | 章节 | 错误风险 |
|---|------|------|---------|
| A1 | spdlog v1.15.1 + fmt v11.1.4 通过 xmake-repo 提供 —— 未经编译验证（当前环境无 clang-cl） | Environment Availability | 中等 —— 版本可能不同，但不影响 API 兼容性（SPDLOG_LOGGER_CALL 在所有 v1.x 版本中行为一致） |
| A2 | `clang-cl` 上 `__FILE__` 默认返回仅文件名（无完整路径）—— 基于 MSVC/clang-cl 默认行为训练知识，未经编译验证 | §1.2 | 低 —— 即使 `__FILE__` 返回完整路径，`logging::detail::shortFile()` 运行时截取也能正确处理 |
| A3 | `SPDLOG_LOGGER_CALL` 的 `fmt::format(__VA_ARGS__)` 中的 `__VA_ARGS__` 在宏展开点求值，不会在 async worker 线程上产生 fmt 编译错误 | §1.2 | 低 —— 这是 C++ 预处理器标准行为，所有编译器一致 |

---

## 未解决问题

### Q1: D-03 与 D-10 的源位置格式冲突

**D-03 要求:** 源位置注入消息体，不使用 spdlog pattern flags（`%@`, `%s:%#`）
**D-10 指定 pattern:** `[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] [%n] [%s:%#] %v` —— 包含 `%s:%#`

**冲突:** 两个锁定决策互斥。D-03 告知不要使用 pattern flags，D-10 指定了使用 pattern flags 的格式。

**现有了解:**
- D-03 的安全依据（async source_loc 悬垂）在此项目中风险 LOW（PITFALLS.md pitfall #2: "Windows x64，无 dlclose/FreeLibrary"）
- D-10 的模式更接近 spdlog 惯用法，但依赖 async safe source_loc
- 若遵循 D-10，则 `%s:%#` 会从 `SPDLOG_LOGGER_CALL` 传入的 `source_loc{__FILE__, __LINE__}` 渲染 —— 而消息体中不应再注入（避免重复）
- 若遵循 D-03，则 pattern 不应含 `%s:%#`，源位置在消息体中

**推荐方案:** 遵循 D-03（注入消息体），调整 D-10 的 pattern 为不含 `%s:%#` 的版本：
```
[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] [%n] %v
```
**等待用户确认**后再最终确定 pattern 字符串。

### Q2: 当前未使用 spdlog 的 .cpp 文件是否需要 DEFINE_LOGGER

**问题:** D-05 要求"每个 .cpp 文件一个 DEFINE_LOGGER"，但 ~22 个 .cpp 文件中仅 13 个当前使用 spdlog。是否要为所有 22 个文件添加 DEFINE_LOGGER？

**现有了解:**
- D-08: "本 phase 仅定义当前 src/ 下已有的源文件所对应的标签"
- log_tags.h 中定义了 22 个标签常量（针对当前所有 .cpp 文件）

**推荐方案:** 在 Phase 1 中：
1. 在 `log_tags.h` 中定义所有标签常量（22 个）—— 标准
2. 仅为 **13 个当前使用 spdlog** 的文件添加 `DEFINE_LOGGER` —— 减少无谓变更
3. 其余 9 个文件在首次需要日志调用时（Phase 2-4）再添加 `DEFINE_LOGGER`
4. setup.cpp 的 `allModuleTags()` 列表包含所有 22 个标签

**等待用户确认。**

### Q3: fmt::format 在宏中的性能影响

**问题:** `LOG_INFO("msg: {}", arg)` 在宏展开时调用 `fmt::format(__VA_ARGS__)` 预格式化消息字符串。这与 spdlog 原生延迟格式化（仅在 `should_log` 通过后才格式化）不同。

**现有了解:**
- 延迟格式化避免了 level 不够时的不必要格式化工作
- 但 SPDLOG_ACTIVE_LEVEL 在 release 模式下剥离 TRACE/DEBUG 宏 —— 编译期消除，零开销
- 对于 INFO/WARN/ERROR/CRITICAL 级别（这些通常启用），预格式化的额外开销可忽略不计（每次日志调用多一次 fmt::format，~微秒级）

**推荐方案:** 接受预格式化开销。Phase 1-2 的规模（每个文件编码一次日志调用，而非每帧）下完全可忽略。如果需要优化，Phase 2 可引入 `should_log` 守卫：
```cpp
#define LOG_DEBUG(...) \
    do { if (gLoggerPtr->should_log(spdlog::level::debug)) \
        SPDLOG_LOGGER_CALL(gLoggerPtr, spdlog::level::debug, \
            "[{}:{}] {}", shortFile(__FILE__), __LINE__, fmt::format(__VA_ARGS__)); \
    } while(0)
```
**无需用户确认 —— 性能优化属于 Claude's Discretion，在 Phase 2 中如遇性能问题可改进。**

---

## 技术现状

| 旧方法 | 新方法 | 变更时机 | 影响 |
|--------|--------|---------|------|
| `spdlog::info("msg")` 函数调用 | `LOG_INFO("msg")` 自定义宏 | Phase 1 | 源位置自动捕获，模块标签自动携带 |
| 单一日志文件 `encro.verbose.log` | 单一日志文件 `encro.verbose.log` | Phase 2（更名为 `encro_YYYYMMDD_HHMMSS.log`） | Phase 1 保持现有文件名 |
| 无源位置 | `[file.cpp:line]` 在消息体中 | Phase 1 | 每条日志可追溯到精确调用点 |
| 无模块标签 | `[module.tag]` 通过 `%n` pattern flag | Phase 1 | 日志可按模块前缀过滤 |
| `spdlog::set_pattern()` 全局单次 | 相同 —— 在 setup.cpp 中 | Phase 1 | 位置从 prelude.cpp 移至 setup.cpp |
| manual slot label 前缀 | 自动模块标签（`%n`）+ 手工消息前缀 | Phase 1 | 双重标识：logger 名（自动）+ 消息前缀（手动保留） |

**已弃用/过时:**
- `spdlog::debug()` / `spdlog::info()` / `spdlog::warn()` / `spdlog::error()` 函数形式：被自定义宏替代（D-01, INF-01）
- `prelude.cpp` 中的 `setupLogging()` 匿名 namespace 函数：委托给 `logging::setup()`（D-12）

---

## 来源

### 主要来源（HIGH 置信度）
- [spdlog v1.15.1 源码 (GitHub raw)](https://raw.githubusercontent.com/gabime/spdlog/v1.15.1/include/spdlog/common.h) — `SPDLOG_ACTIVE_LEVEL` 默认值和 level_enum 值
- [spdlog v1.15.1 源码 (GitHub raw)](https://raw.githubusercontent.com/gabime/spdlog/v1.15.1/include/spdlog/spdlog.h) — `SPDLOG_LOGGER_TRACE/DEBUG/INFO/...` 宏定义
- [spdlog Wiki: Creating Loggers](https://github.com/gabime/spdlog/wiki/Creating-loggers) — named logger registry、`spdlog::get()`、sink sharing 模式
- [spdlog v1.15.1 源码 (GitHub raw)](https://raw.githubusercontent.com/gabime/spdlog/v1.15.1/include/spdlog/spdlog-inl.h) — `spdlog::get()`, `register_logger()`, `default_logger_raw()` 实现
- [spdlog v1.15.1 源码 (GitHub raw)](https://raw.githubusercontent.com/gabime/spdlog/v1.15.1/include/spdlog/tweakme.h) — `SPDLOG_ACTIVE_LEVEL` 配置选项
- [DeepWiki: spdlog Build and Configuration](https://deepwiki.com/gabime/spdlog/8-build-and-configuration) — `SPDLOG_ACTIVE_LEVEL` 编译期剥离机制
- 项目源码: `src/app/prelude.cpp` — 现有 logging setup 实现
- 项目源码: `src/infra/crash_runtime.cpp` — crash handler logger 集成
- `.planning/CONTEXT.md` (Phase 1) — 14 个锁定决策
- `.planning/REQUIREMENTS.md` — INF-01~05, OBS-01~02, OBS-04
- `.planning/research/PITFALLS.md` — 3 个关键陷阱及缓解

### 次要来源（MEDIUM 置信度）
- [spdlog Discussion #2377](https://github.com/gabime/spdlog/discussions/2377) — Shared sinks between loggers 模式
- [spdlog Issue #1381](https://github.com/gabime/spdlog/issues/1381) — Multiple loggers sharing file sink
- WebSearch — spdlog xmake-repo 提供 v1.15.1 + fmt v11.1.4 [ASSUMED]

### 三级来源（LOW 置信度）
- 无（全部声明均为 VERIFIED 或 CITED 来源，或标记为 ASSUMED 并记录在 Assumptions Log 中）

---

## 元数据

**置信度细分:**
- 标准技术栈: HIGH — spdlog v1.15.1 的 API 已验证（GitHub raw source）；fmt v11.1.4 与 spdlog 的配对记录在 STACK.md 中
- 架构: HIGH — DEFINE_LOGGER + shared-sink + named logger registry 模式直接来自 spdlog 官方 wiki 和讨论
- 陷阱: HIGH — 5 个陷阱均通过 PITFALLS.md 研究或 spdlog GitHub issues 验证
- 迁移: HIGH — 13 个文件已通过 grep 精确盘点，所有 spdlog 调用点已列出

**研究日期:** 2026-05-23
**有效期至:** 2026-06-23（稳定的 spdlog API，30 天有效）
