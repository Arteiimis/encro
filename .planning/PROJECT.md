# Encro — 日志系统优化

## What This Is

对 encro CLI 媒体编码工具的日志系统进行全面增强。让每一条日志都携带来源位置、模块标签、阶段耗时，出错时提供完整的操作链路和环境快照。日志文件按每次运行独立存储，保留最近 10 次。目标是让 debug 时看一眼日志就能定位问题，不需要复现。

## Core Value

每条日志都能回答三个问题：从哪来的、在干什么、花了多久。

## Requirements

### Validated

- ✓ spdlog async 日志框架 — 现有
- ✓ `--verbose` / `--verbose-echo` 控制日志开关 — 现有
- ✓ 日志落盘 `%LOCALAPPDATA%/encro/logs/` — 现有
- ✓ crash handler 集成 logger（critical 级别 flush） — 现有
- ✓ 外部 FFmpeg/FFprobe 工具链 — 现有
- ✓ BS::thread_pool 并行编码 — 现有
- ✓ Catch2 v3 测试框架 — 现有
- ✓ xmake + clang-cl/lld-link 构建 — 现有

### Active

- [ ] **LOG-01**: 每条日志自动包含源文件路径和行号（`file.cpp:128`）
- [ ] **LOG-02**: 每条日志自动包含模块/组件标签（`[video.encode]`, `[pack.zip]` 等层级标签）
- [ ] **LOG-03**: pipeline 各阶段（scan → probe → encode → pack）自动计时并记录耗时
- [ ] **LOG-04**: 出错时输出完整操作链路回溯（处理哪个文件 → 哪个阶段 → 第几次重试 → 具体错误）
- [ ] **LOG-05**: 出错时输出环境快照（并发槽位状态、已处理/剩余文件数、FFmpeg 进程信息）
- [ ] **LOG-06**: 每次运行生成独立日志文件（按时间戳命名 `encro_20260523_143052.log`）
- [ ] **LOG-07**: 自动清理，只保留最近 10 个日志文件
- [ ] **LOG-08**: 可选 `--log-json` 输出结构化 JSON 日志（方便工具解析分析）
- [ ] **LOG-09**: 定义并实施模块标签命名规范（层级式，如 `video.encode`, `video.probe`, `pack.zip`）

### Out of Scope

- 不改变 spdlog 以外的日志库 — spdlog 是最优选择，不引入替代品
- 不改为远程日志/集中式日志收集 — 纯本地文件日志
- 不改变业务逻辑（编码/pack/scan） — 仅在日志层增强
- 不添加实时日志查看/Web dashboard — 本次聚焦文件日志质量

## Context

**现有日志系统（`src/app/prelude.cpp`）：**
- 单一日志文件 `encro.verbose.log`，所有运行共用
- 日志格式：`[时间戳] [级别] 消息` — 无来源位置、无模块标签、无耗时
- spdlog async_logger，单线程 pool（8192 队列），flush_on error
- 仅在 `--verbose` 时启用，否则 `spdlog::level::off`
- 19 个源文件分散使用 `spdlog::debug/info/warn/error`，无统一规范

**并发现状：**
- `parallel::runIndexedTasks` 用 `BS::pause_thread_pool` 调度编码任务
- 各 slot 手动加 `[slot:X task:Y/Z]` 前缀做日志区分
- spdlog async 单线程序列化保证行级不交错，但缺乏按文件/task 的追踪能力
- `video_batch_execution.cpp` 中已手动记录 elapsedMs

**代码库规模：**
- ~30 个源文件，~37 个测试文件
- `prelude.cpp` 是日志初始化的唯一入口（`setupLogging()`）
- `crash_runtime.cpp` 在崩溃时直接调 `logger->critical()` + `logger->flush()`

## Constraints

- **Tech stack**: spdlog 生态内（不引入其他日志库），C++26，xmake，clang-cl/lld-link
- **性能**: 日志必须保持 async，不能阻塞主流程或编码 pipeline
- **兼容性**: 不改动业务逻辑文件的核心行为，日志调用点可微调但语义不变
- **平台**: 主要 Windows x64，POSIX 路径保留兼容

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| 保留 spdlog，在其上增强 | 已深度集成，async 性能好，生态成熟 | — Pending |
| 源文件位置用 `__FILE__` + `__LINE__` 宏自动注入 | 零运行时开销，编译期确定 | — Pending |
| 阶段计时用 RAII scoped timer | 自动记录进入/离开，异常安全 | — Pending |
| 日志文件按次独立 + 保留最近 10 个 | 兼顾可追溯性和磁盘占用 | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd:complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-05-23 after initialization*
