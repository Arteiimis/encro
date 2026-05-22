# Phase 2: File Management + Runtime Observability - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning

## Phase Boundary

每次 CLI 调用生成一个独立的、带时间感知的日志文件，自动生命周期管理，并在 pipeline 各阶段记录耗时。Phase 1 的宏和标签体系已经就位 —— 本 phase 建立在其上，让日志具备"每次运行可追溯"和"每个阶段可度量"的能力。

## Implementation Decisions

### Per-Run File Naming
- **D-01:** 时间戳格式 `encro_YYYYMMDD_HHMMSS.log`，在 `logging::setup()` 调用时捕获时间点。与 ROADMAP success criterion #1 一致 — 每次运行独立文件，秒级精度。
- **D-02:** 同一秒内多次启动的极低概率碰撞 → 追加 PID 后缀：`encro_YYYYMMDD_HHMMSS_PID.log`。零成本保险，不改动正常路径的文件名。
- **D-03:** File sink 在启动时创建，运行期间不变更。避免 Pitfall #6（async queue 与 sink 替换的竞争）。不引入 mid-run rotation 或 sink 切换。

### Retention Cleanup
- **D-04:** 清理在创建新日志文件**之前**执行（启动早期）。Pitfall #6 预防措施 — 避免清理逻辑与活跃 logger 竞争。
- **D-05:** 匹配模式：`encro_*.log` + `encro_*.log.*`（后者捕获 rotation 产生的 `.log.1`, `.log.2` 等后缀文件）。不匹配其他文件。
- **D-06:** 按文件名字典序排序（时间戳前缀天然等于时间顺序），保留最近 10 个，删除其余。不依赖文件系统元数据（mtime 可能被文件操作修改）。
- **D-07:** 只删除匹配 encro 模式的文件。日志目录中可能存在的其他应用文件不受影响。

### RAII ScopedTimer
- **D-08:** `ScopedTimer` 类放在 `src/logging/logging.h`，与 LOG_* 宏同级。构造函数记录 `"[stage_name] begin"` (LOG_INFO)，析构函数记录 `"[stage_name] completed in Xms"` (LOG_INFO)。Phase 1 D-13 已为此预留位置。
- **D-09:** 阶段名称：自由格式 `std::string_view`，由开发者描述阶段。不做受控词汇表 — 阶段名称是文档性的，不是机器可解析的。
- **D-10:** 使用 `std::chrono::steady_clock` 测量耗时。Pitfall #4 — 用 monotonic clock 测时长，不与 spdlog 的 system_clock 时间戳混淆。日志行自带的 spdlog 时间戳标记"何时"，ScopedTimer 的 elapsed 标记"多久"。
- **D-11:** 析构函数 `noexcept` — 保证异常展开时仍然记录耗时。正常路径和异常路径都产生完整的计时日志。
- **D-12:** 嵌套自然支持 — 每个 ScopedTimer 独立持有 start time point，嵌套作用域产生层级日志输出。外层阶段的总耗时自然包含内层阶段。

### Crash Handler Direct File Write
- **D-13:** `logging::currentLogFilePath()` 访问器暴露当前运行的日志文件路径。存储在 `src/logging/setup.cpp` 模块级变量中，在 `logging::setup()` 中设置。
- **D-14:** Crash handler 直接通过 `std::ofstream` 追加写入日志文件（`std::ios::app`），完全绕过 spdlog。Pitfall #10 的核心解决方案 — crash 可能在 spdlog thread pool 关闭后发生。
- **D-15:** 直接写入使用手动构造的格式，匹配 spdlog pattern：`[timestamp] [critical] [infra.crash] [file:line] crash message`。不调用 spdlog API。
- **D-16:** Crash handler 回退链：direct file append → spdlog::default_logger_raw() → stderr。保持现有的三层防御，新增第一层为 per-run 文件专用。

### Rotating File Sink
- **D-17:** 使用 `rotating_file_sink_mt`，单文件 10 MB 上限，保留 3 个轮转文件（per FILE-03）。对于典型的几分钟 CLI 运行，轮转几乎不会触发 — 这是防御性配置。
- **D-18:** 轮转产生的文件命名由 spdlog 内置逻辑处理（`encro_20260523_143052.log.1`, `.log.2` 等）。清理逻辑的 `encro_*.log.*` 模式覆盖这些文件。

### Stage Definitions
- **D-19:** Video pipeline 阶段: scan → probe → encode → pack。Picture pipeline 阶段: scan → compress → pack。与 ROADMAP 定义的 pipeline 架构对齐。
- **D-20:** ScopedTimer 放置在 pipeline 编排代码的函数入口点：`video_process.cpp`（scan/probe/encode/pack 分发）、`picture_process.cpp`（scan/compress/pack 分发）、`pack_service.cpp`（pack 操作）。不放在底层工具函数中 — 保持在有意义的人类可读粒度。

### Log Directory Fallback
- **D-21:** 强化现有回退逻辑（`setup.cpp` lines 121-131）：primary log dir 失败 → fallback 到 system temp dir。Temp dir 也失败 → 仅 stderr console sink，不创建 file sink，不阻塞主流程。FILE-05 要求"日志基础设施故障不阻塞编码工作流"。
- **D-22:** Terminal 始终在回退激活时警告用户：`"Warning: Using temporary log directory: <path>"`。

### Signal Handler Safety
- **D-23:** 不在 SIGINT/Ctrl+C 信号处理器中添加日志调用。保持现有的 `stopsignal` atomic flag 模式 — 信号处理器只设置标志，主循环检查标志并记录。Pitfall 集成陷阱 #3 — spdlog 内部使用 mutex，从信号处理器调用会导致死锁。

### Claude's Discretion
- **ScopedTimer 移动语义:** ScopedTimer 只移动不复制 — 删除拷贝构造/赋值，保留移动。防止双重记录（两个析构函数都会写日志）。
- **清理时机:** 清理只在启动时运行一次，不在运行期间后台执行。在长期批处理中，如果 log 产生但未达到轮转大小，不会触发 mid-run 清理。未来的增强可以添加每次轮转后检查（v2）。
- **DEFINE_LOGGER for Phase 2 files:** 新增源文件（如有）遵循 Phase 1 D-05/D-06 模式 — 每个 .cpp 一个 DEFINE_LOGGER，使用已有的一级标签（如 `logtags::APP_ENTRY`）或新增 Phase 2 专属标签。
- **队列大小:** 保持 8192 不变。Phase 2 新增的 ScopedTimer 只在阶段边界产生日志（每个文件 4-6 条），不显著增加吞吐量。性能回归由测试覆盖。

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements & Roadmap
- `.planning/ROADMAP.md` — Phase 2 完整定义，5 条 success criteria，依赖 Phase 1
- `.planning/REQUIREMENTS.md` — FILE-01~05, OBS-03 详细说明
- `.planning/PROJECT.md` — Core Value, Constraints, Key Decisions

### Phase 1 Foundation (downstream MUST understand what's already built)
- `.planning/phases/01-logging-foundation/01-CONTEXT.md` — Phase 1 全部决策（宏设计 D-01~D-14，标签体系，source location 方案）
- `.planning/phases/01-logging-foundation/01-RESEARCH.md` — Phase 1 技术调研
- `.planning/phases/01-logging-foundation/01-01-PLAN.md` — Plan 1 (infrastructure + setup.cpp)
- `.planning/phases/01-logging-foundation/01-02-PLAN.md` — Plan 2 (build system + prelude refactoring)
- `.planning/phases/01-logging-foundation/01-03-PLAN.md` — Plan 3 (13-file migration to LOG_*)
- `.planning/phases/01-logging-foundation/01-04-PLAN.md` — Plan 4 (DEFINE_LOGGER for 10 more files)

### Research (pre-roadmap analysis)
- `.planning/research/PITFALLS.md` — 10 个关键陷阱及预防措施。Phase 2 相关：Pitfall #4 (clock drift), #6 (file rotation + async drain), #10 (crash handler + per-run files)
- `.planning/research/SUMMARY.md` — 技术架构总览
- `.planning/research/STACK.md` — spdlog/fmt 版本兼容性

### Codebase Maps
- `.planning/codebase/ARCHITECTURE.md` — 系统架构，组件职责，数据流
- `.planning/codebase/INTEGRATIONS.md` — 日志系统与 crash handler、terminal 的集成点
- `.planning/codebase/CONVENTIONS.md` — 命名、代码风格约定

### Key Source Files (Phase 2 modification surface)
- `src/logging/setup.cpp` — Phase 1 已交付的 setup/shutdown；Phase 2 在此修改文件命名、sink 类型、清理逻辑
- `src/logging/logging.h` — Phase 1 已交付的宏和 DEFINE_LOGGER；Phase 2 在此添加 ScopedTimer
- `src/logging/log_tags.h` — Phase 1 已交付的标签常量；Phase 2 可添加新标签（如需要）
- `src/infra/crash_runtime.cpp` — 崩溃处理器；Phase 2 添加 direct file append 路径
- `src/app/prelude.cpp` — 日志初始化的调用方；Phase 2 可能需要适配新的 setup 接口
- `src/video/video_process.cpp` — video pipeline 编排；Phase 2 在此添加 ScopedTimer
- `src/picture/picture_process.cpp` — picture pipeline 编排；Phase 2 在此添加 ScopedTimer
- `src/pack/pack_service.cpp` — pack 操作编排；Phase 2 在此添加 ScopedTimer
- `xmake.lua` — 构建配置；Phase 2 可能需要添加测试文件或链接

## Existing Code Insights

### Reusable Assets
- **`logging::setup(LogConfig)` / `logging::shutdown()`:** `src/logging/setup.cpp` — Phase 1 已交付的集中式日志初始化。Phase 2 修改文件命名和 sink 类型，保持接口不变。
- **`LogConfig` struct:** 已有 `verboseEnabled`, `verboseEchoEnabled`, `colorsEnabled`, `customLogDir` 字段。Phase 2 可能需要添加 `logJsonEnabled` 等字段（但 JSON 输出属于 Phase 4 — 仅为未来预留设计空间）。
- **`resolveCommonLogDir()`:** `src/logging/setup.cpp:45-63` — 已实现 Windows（LOCALAPPDATA → APPDATA → TEMP）和 POSIX（HOME → TEMP）回退链。Phase 2 强化回退逻辑，确保永不为空。
- **`crash::tryWriteToLogger()`:** `src/infra/crash_runtime.cpp:29-37` — 现有的三层回退（logger → stderr）。Phase 2 新增 direct file append 作为第一优先路径。
- **`video_batch_execution.cpp` 已有手动计时:** `elapsedMs` 的手动记录已是现有模式 — Phase 2 用 ScopedTimer 标准化。

### Established Patterns
- **East const, trailing return type, anonymous namespaces** — 与 Phase 1 相同约定
- **`src/` relative includes** — `#include "logging/logging.h"`
- **Free functions + RAII classes** — ScopedTimer 是 Phase 2 中唯一的类
- **Shared sink architecture** — Phase 1 所有 named logger 共享同一组 sink。Phase 2 保持不变。
- **`noexcept` destructors** — 现有代码中的 RAII 类型全部使用 noexcept 析构。ScopedTimer 遵循此约定。

### Integration Points
- **`logging::setup()` → `prelude::setupLogging()`:** 调用链不变。`prelude.cpp` 传入 `LogConfig`，接收新的时间戳文件路径。
- **`logging::currentLogFilePath()` → `crash_runtime.cpp`:** 新增访问器。Crash handler 调用此函数获取文件路径进行直接追加写入。
- **Pipeline orchestration → ScopedTimer:** `video_process.cpp`, `picture_process.cpp`, `pack_service.cpp` 在函数入口处声明 ScopedTimer。
- **Cleanup → `resolveCommonLogDir()`:** 清理使用相同的目录解析逻辑，在文件创建前扫描和删除旧文件。
- **`xmake.lua`:** 可能需要为 ScopedTimer 测试添加新的 test target。Phase 2 不改变 SPDLOG_ACTIVE_LEVEL 配置（Phase 1 D-14 已完成）。

## Specific Ideas

No specific references or "do it like X" examples — Phase 2 follows established spdlog patterns:
- `rotating_file_sink_mt` 是 spdlog 内置 sink，API 稳定
- `ScopedTimer` 是经典 RAII 模式，与现有代码库风格一致
- 文件命名和清理逻辑与 RESEARCH/PITFALLS.md 中记录的防护措施对齐

## Deferred Ideas

None — discussion stayed within phase scope.

---

*Phase: 2-File Management + Runtime Observability*
*Context gathered: 2026-05-23*
