---
title: "Pack模块简化设计决策"
date: 2026-04-30
context: "/gsd-explore — v1.3 Pack Subsystem OO Refactor 完成后提出的接口简化探索"
---

# Pack模块简化设计决策

## 背景

v1.3 完成了 pack 模块的 OOP 迁移（Packer + PackService + IPacker），但调用方仍然需要手动编排打包流程：
了解分组逻辑（Packer::groupPackEntriesWithSubparts）、命名逻辑（PackService::buildGroupOrdinalRanges）、
手动组装 PackPlan、设置进度回调——所有编排暴露在调用方。同时 `pack::detail::` 内部类型泄漏到了外部消费者。

## 决策

### D-01: 单一入口 — PackRequest 声明式 API

**方案 C: `pack::execute(PackRequest)` 声明式入口**

调用方只声明"需求描述"（哪些文件、输出到哪、命名偏好），所有分组、命名、Plan构建、执行编排
完全由 pack 模块内部完成。

**理由：**
- 调用方代码从数十行手动编排缩减为1-2行声明
- PackPlan、buildGroupOrdinalRanges、pack::detail:: 类型降级为模块内部实现
- 与最初的 CLI 配置驱动设计一致（打包配置与媒体类型无关）

### D-02: 分组策略统一为两层切分

Picture 和 Video 均使用 `groupPackEntriesWithSubparts`（两层切分），取代 Picture 单独使用
subparts + Video 单独使用单层 `groupPackFiles` 的不一致现状。

**理由：**
- 两层切分是超集——单层切分只是从未触发第二层（video 总是先触及大小限制而非文件数限制）
- 减少代码路径和测试矩阵

### D-03: 命名规则内化到 pack 模块

zip 文件命名（`dirname_part1.2.zip` / `encoded_videos_part1.zip`）和 zip 条目命名
（flat vs keep 结构、冲突处理）均由 pack 模块内部完成，调用方提供命名偏好参数。

保留可选的 `namingStrategy` 钩子（类型擦除的 callback 或 enum + 预置策略）供未来扩展，
但当前调用方均不使用自定义钩子。

### D-04: 配置来自 CLI，调教统一注入

`compact`、`maxParallelJobs`、`outputDir`、`forceNameConflictHandling` 等配置项
均从 `AppConfig`（CLI参数推导）获取，统一注入 PackRequest，修复 Picture 路径
硬编码 `compact=true` 的问题（应读取 `!config.fullProgress`）。

## 约束

- **完全不影响现有程序行为** — 所有 945 断言通过的测试在重构后保持绿
- **同名 zip 条目冲突处理逻辑不变** — 现有的 `uniqueifyZipEntryNames`、`forceNameConflictHandling` 路径保持
- **恢复性执行（jobState）不变** — `selectPackPlanIndexes` 的跳过已完成逻辑保持
- **IPacker 接口保留** — 仍可通过 MockPacker 进行单元测试

## 关联

- 探索对话: `/gsd-explore` (2026-04-30)
- Todo: `.planning/todos/pending/pack-simplify-single-entry.md`
- Seed: `.planning/seeds/oop-refactor-remaining-modules.md`
