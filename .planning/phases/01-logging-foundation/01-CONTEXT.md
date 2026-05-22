# Phase 1: Logging Foundation - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning

## Phase Boundary

建立统一的日志宏层和模块标签体系。让每一条日志自动携带来源位置和模块标识 —— 开发者写 `LOG_INFO("msg")` 即可获得 `file.cpp:128 [video.encode] msg`，所有日志配置集中在一处。这是四阶段日志增强的地基层 —— 后续的文件管理、取证诊断、JSON 输出都建立在此层之上。

## Implementation Decisions

### Macro Design & Naming
- **D-01:** 自定义 `LOG_TRACE/LOG_DEBUG/LOG_INFO/LOG_WARN/LOG_ERROR/LOG_CRITICAL` 宏封装层，内部使用 `SPDLOG_LOGGER_CALL`。**不使用 spdlog 内建 `SPDLOG_INFO` 等裸宏** —— 自定义层支持在 call site 注入模块标签和源位置到消息体，为 Phase 3 的 error context chaining 预留注入点。
- **D-02:** 宏命名不加前缀（不用 `ENCRO_INFO`），直接使用 `LOG_INFO`。放在 `src/logging/logging.h` 命名空间内，业务代码通过 `using namespace logging::macros;` 或限定名使用。

### Source Location Capture
- **D-03:** 源位置直接注入消息体（格式 `file.cpp:128`），**不使用 spdlog pattern flags（`%@`, `%s:%#`）**。避免 async 模式下 source_loc 裸指针的悬垂风险（PITFALLS.md pitfall #2）。`__FILE__` + `__LINE__` 在宏展开点求值，编译期零运行时开销。
- **D-04:** 使用 `__FILE__` 的短文件名（仅文件名，不含路径）以保证日志可读性。通过 `SPDLOG_SHORT_FILE` 或等效方式截取。

### Logger Granularity
- **D-05:** 每个 .cpp 文件一个 `DEFINE_LOGGER("module.tag")` 调用。~19 个文件各注册独立 named logger，全部共享同一组 sink（1 file + optional console）。ROADMAP success criterion #2 明确要求此模式。
- **D-06:** Logger 指针缓存在模块级静态变量中（`static auto* logger = spdlog::get(name)`），避免每次日志调用时做 hash-map 查找。

### Module Tags Convention
- **D-07:** 所有标签常量定义在单个头文件 `src/logging/log_tags.h`，使用 dot-notation 层级命名。现有一级标签：`video.encode`, `video.probe`, `video.info`, `video.output`, `video.batch`, `video.progress`, `picture.process`, `picture.compress`, `pack.zip`, `pack.service`, `core.scan`, `core.job`, `core.task`, `core.parallel`, `app.entry`, `app.prelude`, `app.pipeline`, `cmd.config`, `infra.toolchain`, `infra.crash`, `infra.signal`, `utils.subprocess`。
- **D-08:** 本 phase 仅定义当前 src/ 下已有的源文件所对应的标签。Phase 2-4 新增的模块标签由各自 phase 添加。

### Migration Strategy
- **D-09:** 一次性迁移全部 13 个使用 spdlog 的源文件。工具链为机械替换：`spdlog::info(...)` → `LOG_INFO(...)`，`spdlog::debug(...)` → `LOG_DEBUG(...)` 等。不保留任何混合状态。

### Log Pattern
- **D-10:** 日志格式字符串: `[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] [%n] [%s:%#] %v`，其中 `%n` 渲染 named logger 名称（即模块标签），`%s` 和 `%#` 由 spdlog 从传递给 async_logger 的 source_loc 结构体渲染。最终输出示例: `[2026-05-23T14:30:52.123+08:00] [info] [video.encode] [codec_transcoding.cpp:247] Starting encode pass 1`
- **D-11:** Level 使用 spdlog 内置的颜色标记 `%^%l%$` 保持与现有终端输出兼容。

### Logging Infrastructure Location
- **D-12:** 所有 sink 创建、logger 注册、线程池初始化的代码全部迁移至新的 `src/logging/setup.cpp`。`prelude.cpp` 中现有 `setupLogging()` 函数重构为调用 `logging::setup()`。
- **D-13:** 公共头文件 `src/logging/logging.h` 导出：宏定义、`DEFINE_LOGGER`、`ScopedTimer`（Phase 2 使用）。标签常量在独立头文件 `src/logging/log_tags.h`。

### SPDLOG_ACTIVE_LEVEL
- **D-14:** 在 `xmake.lua` 的 release/releasedbg 模式下定义 `SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO`，debug/coverage 模式下设为 `SPDLOG_LEVEL_TRACE`。Release 构建中 trace/debug 日志被编译器完全剥离，零运行时开销。

### Claude's Discretion
- **日志级别选择:** 各调用点保留现有日志级别（`info` → `LOG_INFO`, `debug` → `LOG_DEBUG` 等），不做级别调整。级别调整属于独立行为变更，不属于本 phase 的日志层增强。
- **DEFINE_LOGGER 的位置:** 放在每个 .cpp 文件顶部，紧接 `#include` 块之后、匿名 namespace 之前。保持代码扫描一致性。

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements & Roadmap
- `.planning/ROADMAP.md` — Phase 1 完整定义、success criteria、依赖关系
- `.planning/REQUIREMENTS.md` — 全量 v1 requirements（INF-01~05, OBS-01~02, OBS-04），含详细说明
- `.planning/PROJECT.md` — Core Value、Constraints、Key Decisions、现有日志系统现状

### Research (pre-roadmap analysis)
- `.planning/research/SUMMARY.md` — 技术架构方案、关键决策依据、推荐技术栈配置
- `.planning/research/PITFALLS.md` — 3 个关键陷阱及预防措施（macro vs function, async source_loc, TLS context）
- `.planning/research/FEATURES.md` — 各 feature 的技术可行性分析
- `.planning/research/STACK.md` — spdlog/fmt/boost::json 版本兼容性和约束

### Codebase Maps
- `.planning/codebase/ARCHITECTURE.md` — 现有系统架构、组件职责、数据流
- `.planning/codebase/CONVENTIONS.md` — 命名、代码风格、namespace 约定
- `.planning/codebase/INTEGRATIONS.md` — 日志系统与 crash handler、terminal 的集成点

### Key Source Files (current logging surface)
- `src/app/prelude.cpp` — 现有 `setupLogging()` 入口（sink 创建、线程池、default logger）
- `src/infra/crash_runtime.cpp` — 崩溃时 logger->critical() + flush() 调用点
- `src/app/app_entry.cpp` — `logConfigSummary()`（配置摘要日志）
- `xmake.lua` — 构建配置（需要在 release 模式加 `SPDLOG_ACTIVE_LEVEL`）

## Existing Code Insights

### Reusable Assets
- **spdlog async_logger + thread_pool:** `src/app/prelude.cpp:105-118` — 现有异步线程池初始化逻辑可直接复用。Shared-sink 架构正确，只需增加 per-file named loggers。
- **crash handler logger access:** `src/infra/crash_runtime.cpp:29-37` — `tryWriteToLogger()` 通过 `spdlog::default_logger_raw()` 访问 logger。迁移到 named loggers 后需要确认 crash handler 能访问当前 run 的 file sink。
- **terminal color integration:** `src/infra/terminal.h` — 现有 `colorsEnabled()` 判断用于决定是否启用 stdout_color_sink。保持不变。

### Established Patterns
- **East const:** `auto* const logger = ...`
- **Trailing return type:** 所有函数使用 trailing return
- **Anonymous namespaces for impl details:** `src/logging/setup.cpp` 内的私有 helper 放匿名 namespace
- **`src/` relative includes:** `#include "logging/logging.h"`
- **Free functions over classes:** 仅 `ScopedTimer` 需要 RAII class，其余均为 free function

### Integration Points
- **`prelude::initStartup()`** → 重构为调用 `logging::setup(config)`，保持接口兼容
- **13 个 spdlog 调用源文件** → 每个添加 `#include "logging/logging.h"` + `DEFINE_LOGGER("tag")`，替换 `spdlog::x()` 为 `LOG_X()`
- **`crash_runtime.cpp`** → 需要知道当前运行的日志文件路径（Phase 2 FILE-04 主要解决，Phase 1 保持现有 stderr fallback）
- **`xmake.lua`** → 所有 target 的 defines 添加 `SPDLOG_ACTIVE_LEVEL`

## Deferred Ideas

None — discussion stayed within phase scope.

---

*Phase: 1-Logging Foundation*
*Context gathered: 2026-05-23*
