# Phase 12: PackRequest 声明式 API & 配置注入 - Context

**Gathered:** 2026-04-30
**Status:** Ready for planning

<domain>
## Phase Boundary

设计 `PackRequest` 声明式入口类型和 `execute()` 单一入口函数，使消费者无需手动编排 `PackPlan`。`PackPlan`、`buildGroupOrdinalRanges`、`pack::detail::` 类型不再对外暴露。配置（compact、outputDir、maxParallelJobs）从 CLI 统一注入。

范围：SIMPLIFY-01~04, 09~10。分组统一和命名内化属于 Phase 13，IPacker 移除属于 Phase 14。
</domain>

<decisions>
## Implementation Decisions

### PackRequest 结构设计
- **D-01:** PackRequest 为单一结构体 + `std::optional` 字段，遵循 designated initializer 风格（非 variant / builder）
- **D-02:** `entries` 字段为 `std::vector<fs::path>` — 最简形式，附加信息（sourceKey、sourceDir）由模块内部从 filesystem 推导
- **D-03:** 包含 `mode` 枚举字段（`Media` / `Directory`），模块根据 mode 选择分组和命名策略
- **D-04:** `NamingConfig` 子结构体（`.layout: OutputLayout`, `.forceConflictHandling: bool`），为 `std::nullopt` 时模块使用 mode 对应的默认策略

### execute() 函数签名
- **D-05:** 自由函数 `pack::execute(PackRequest const&) -> eh::Result<PackRunResult>`，模块内部管理 Packer 生命周期
- **D-06:** jobState 通过 `PackRequest.jobState`（可选指针）传入，非空时 execute() 启用恢复性执行
- **D-07:** 声明在新增的 `src/pack/pack.h` — 唯一公开头文件，消费者只需 `#include "pack/pack.h"`

### 配置注入
- **D-08:** `PackRequest.compact` 为显式 bool 字段，消费者从 `AppConfig.fullProgress` 推导（`compact = !fullProgress`），修复 picture 和 pack-only 硬编码 `compact=true`
- **D-09:** `PackRequest.outputDir` 为必需字段，消费者在调用前完成解析（`config.outputPath.value_or(inputDir) / "packed"`）
- **D-10:** `PackRequest.maxParallelJobs` 为 `std::optional<size_t>`，未设置时 execute() 内部调用 `resolveWorkerCount()` 取默认值

### PackPlan 内部化与恢复性执行
- **D-11:** 恢复性执行逻辑完全内部化到 pack 模块 — execute() 内部处理 mergeTasks + needsExecution + 状态回调（markRunning/Succeeded/Failed）
- **D-12:** `archive_plan.cpp` / `archive_plan.h` 删除，`PreparedPackExecution` 结构体删除
- **D-13:** PackService 静态方法（`buildGroupOrdinalRanges`, `appendOrdinalRangeSuffix`, `selectPackPlanIndexes`, `resolveZipNameForIndex`, `resolveProgressLabelForIndex`）Phase 12 中降级为 private / 匿名命名空间 free function

### Folded Todos
- [pack-simplify-single-entry](.planning/todos/pending/pack-simplify-single-entry.md) — "Pack模块接口简化：PackRequest单一入口"（resolves_phase: 12）

### the agent's Discretion
- PackRequest 其余 minor 字段（如 `removeOnFailure`）由实现阶段确定
- `pack.h` 的具体 include 结构和 PackRequest 字段声明顺序
- execute() 内部 Packer/PackService 的构造方式（堆 vs 栈分配，Phase 14 后直接持有）
</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Pack 模块现有公开 API
- `src/pack/pack_types.h` — PackPlan / PackFileEntry / PackRunResult / PackProgressCallbacks 类型定义
- `src/pack/packer_types.h` — detail 类型（PackEntryInput, PackGroupInput, PackEntryPartition）
- `src/pack/pack_service.h` — PackService 公开 API（所有静态方法清单）
- `src/pack/packer.h` — Packer 公开 API（group* 方法, packFilesToZip, buildDirectoryPackPlan）

### 消费者（需适配）
- `src/picture/picture_process.cpp` — Picture 打包路径，当前手动编排 PackPlan
- `src/video/video_process.cpp` — Video 打包路径，当前手动编排 PackPlan
- `src/video/video_output_planning.cpp` — Video 分组逻辑，当前调用 packer.groupPackFiles()
- `src/app/pipeline.cpp` — pack-only 路径，当前使用 runDirectoryPackWorkflow()
- `src/core/archive_plan.cpp` — 恢复性执行，当前操作 PackPlan（待删除）

### 需求与设计
- `.planning/REQUIREMENTS.md` — SIMPLIFY-01~04, 09~10 需求规格
- `.planning/todos/pending/pack-simplify-single-entry.md` — PackRequest 设计 todo（折叠到此阶段）
- `.planning/PROJECT.md` §Current Milestone — v1.4 目标概要

### 规范
- `.planning/codebase/CONVENTIONS.md` — designated initializer、eh::Result<T>、trailing return type 规范
- `.planning/codebase/STACK.md` — libzippp、Catch2、xmake 技术栈
</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
| Asset | Role in Phase 12 |
|-------|-----------------|
| `PackPlan` (aggregate) | 内部化后保留为 execute() 内部实现细节（分组 + 命名 + 进度回调的载体） |
| `PackRunResult` | 保留为 execute() 返回类型（exit code + zip 路径列表） |
| `PackProgressCallbacks` | 保留为内部类型（5 个回调聚合，不再对外暴露） |
| `PackFileEntry` | 保留为内部类型（sourcePath + zipEntryName 映射） |
| `NamingConfig` (新) | 新增子结构体，替代零散的 outputLayout + forceConflictHandling 参数 |

### Established Patterns
- **Designated initializers**（C++20）— PackRequest 构建遵循此风格，不引入 builder
- **`eh::Result<T>`** — execute() 返回值遵循项目统一错误处理模式
- **Trailing return type** — 所有新声明的函数遵循
- **`kDefaultMaxArchiveGroupSize`** — 500 MB 常量保留，execute() 内部使用
- **自由函数优先** — execute() 作为 namespace 级别函数，非类静态方法

### Integration Points

| Consumer | 当前调用方式 | Phase 12 后 |
|----------|------------|------------|
| `picture_process.cpp` | 手动构建 PackPlan + `PackService::packGroups()` | `pack::execute(PackRequest{.mode=Media, ...})` |
| `video_process.cpp` | 手动构建 PackPlan + `PackService::runPackPlan()` | `pack::execute(PackRequest{.mode=Media, ...})` |
| `pipeline.cpp` (pack-only) | `PackService::runDirectoryPackWorkflow()` | `pack::execute(PackRequest{.mode=Directory, ...})` |
| `archive_plan.cpp` | `prepareResumablePackExecution()` | **删除** — execute() 内部处理恢复逻辑 |
| `video_output_planning.cpp` | `packer.groupPackFiles()` | **不再直接调用** — execute() 内部分组 |
</code_context>

<specifics>
## Specific Ideas

无 — 所有决策由讨论中选定，无额外"像 X 那样"的引用或偏好。
</specifics>

<deferred>
## Deferred Ideas

- zip 命名策略完整内部化（Phase 13）
- 分组策略统一为两层切分 — groupPackEntriesWithSubparts（Phase 13）
- IPacker 移除 + MockPacker 删除 + PackService 直接持有 Packer（Phase 14）
- PackRequest builder pattern 语法糖（REQUIREMENTS.md Out of Scope — 当前不需要）
- [remove-ipacker-abstraction](.planning/todos/pending/remove-ipacker-abstraction.md) — resolves_phase: 14，不在此阶段
</deferred>

---

*Phase: 12-packrequest-api*
*Context gathered: 2026-04-30*
