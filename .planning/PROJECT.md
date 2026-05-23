# Encro — 日志系统优化

## What This Is

encro CLI 的日志系统已完成生产级增强。每条日志携带源位置（`file.cpp:128`）、模块标签（`[video.encode]`）、阶段耗时（`completed in Xms`），出错时输出完整操作链路（`input.mkv > encode > retry 2/3 > FFmpeg exit 1`）和环境快照（并发槽位、剩余队列、FFmpeg 进程）。每次运行生成独立日志文件 + 可选 NDJSON 结构化输出，保留最近 10 次运行。

## Core Value

每条日志都能回答三个问题：从哪来的、在干什么、花了多久。—— **已交付，v1.0 验证通过。**

## Current State (v1.0 — shipped 2026-05-23)

- **Phases completed:** 4（13 plans, 26 tasks）
- **Requirements delivered:** 20/20（INF-01~05, OBS-01~04, FILE-01~05, FOR-01~03, TOOL-01~03）
- **Key deliverables:**
  - `LOG_TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL` 宏层，自动注入源位置和模块标签
  - 24 个 named async_logger，共享 file + console sink 架构
  - 时间戳命名的 per-run 日志文件 + 自动保留清理
  - `ScopedTimer` RAII 阶段计时（video/picture/pack 共 7 个阶段边界）
  - Crash handler 3 级回退链（直接文件追加 → spdlog → stderr）
  - `ScopedErrorContext` RAII 错误上下文链（thread-local TLS 栈）
  - `captureEnvironmentSnapshot()` 锁-free 环境快照
  - `--log-json` NDJSON 结构化输出（`JsonFormatter` + companion `.ndjson` 文件）
- **Codebase:** ~340 tests, ~3423 assertions, zero regressions
- **Tech stack:** spdlog 1.15.1 + boost::json + immer + Catch2 v3，C++26，xmake + clang-cl/lld-link

## Requirements

### Validated

- ✓ INF-01: 宏替代直接 spdlog 调用 — v1.0
- ✓ INF-02: `SPDLOG_ACTIVE_LEVEL` 编译期优化 — v1.0
- ✓ INF-03: 层级式模块标签（`video.encode`, `pack.zip`）— v1.0
- ✓ INF-04: 每 .cpp 一个 `DEFINE_LOGGER` — v1.0
- ✓ INF-05: 日志配置集中在 `setup.cpp` — v1.0
- ✓ OBS-01: 每条日志含源文件路径和行号 — v1.0
- ✓ OBS-02: 每条日志含模块标签 — v1.0
- ✓ OBS-03: Pipeline 阶段自动计时（ScopedTimer）— v1.0
- ✓ OBS-04: 统一日志格式 — v1.0
- ✓ FILE-01: Per-run 时间戳日志文件 — v1.0
- ✓ FILE-02: 保留最近 10 个日志文件 — v1.0
- ✓ FILE-03: rotating_file_sink_mt 10MB/3 — v1.0
- ✓ FILE-04: Crash handler 直接文件追加 — v1.0
- ✓ FILE-05: Fallback 到临时目录 — v1.0
- ✓ FOR-01: 操作链路回溯 — v1.0
- ✓ FOR-02: 环境快照 — v1.0
- ✓ FOR-03: Thread-local RAII 错误上下文栈 — v1.0
- ✓ TOOL-01: `--log-json` NDJSON 输出 — v1.0
- ✓ TOOL-02: 自定义 `JsonFormatter`（boost::json）— v1.0
- ✓ TOOL-03: Console 保持人类可读 — v1.0

### Active

（下一里程碑定义）

### Out of Scope

| Feature | Reason |
|---------|--------|
| 远程日志/集中式收集 | 纯本地 CLI 工具 |
| 实时日志查看 dashboard | 本次聚焦文件日志质量 |
| 日志采样/压缩 | 短生命周期 CLI |
| spdlog 替代品 | spdlog 最优 |
| 二进制日志格式 | 文本格式足够 |
| 动态运行时重配置 | CLI flag 配置足够 |

## Context

**v1.0 交付总结：**
- 4 个 phase 在 2 天内执行完毕（discuss → plan → execute 全自动）
- 总计 ~30 个源文件修改，~1200 行新增代码，~500 行测试代码
- 15 个测试文件新建（error_context, snapshot, json）
- 性能影响可忽略：所有格式化在 async worker 线程中完成，宏注入零运行时开销

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| 保留 spdlog，在其上增强 | 已深度集成，async 性能好 | ✓ Good — 全部 4 phases 无依赖变更 |
| `__FILE__` + `__LINE__` 宏注入 | 编译期确定，零运行时开销 | ✓ Good |
| RAII ScopedTimer + ScopedErrorContext | 自动记录，异常安全 | ✓ Good — 模式一致，测试覆盖完整 |
| Per-run 文件 + 保留 10 个 | 可追溯性和磁盘占用平衡 | ✓ Good |
| Error context 用 thread-local 栈 | 无 API 侵入，不依赖 MDC | ✓ Good — FOR-03 精确满足 |
| NDJSON 双 sink 架构 | Per-sink formatter 隔离 | ✓ Good — 修复了 spdlog `set_pattern()` 覆盖 per-sink formatter 的 bug |
| boost::json 序列化 | 自动转义，项目已有依赖 | ✓ Good — CJK/反斜杠/引号/换行符全部正确 |

## Evolution

This document evolves at phase transitions and milestone boundaries.

---

*Last updated: 2026-05-23 after v1.0 milestone*
