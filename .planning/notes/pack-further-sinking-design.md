---
title: "Pack模块进一步下沉设计决策"
date: 2026-05-04
context: "/gsd-explore — PackRequest API 下沉不够彻底，picture_process.cpp 仍然绕过公共接口直接使用 detail/internal/Packer"
---

# Pack模块进一步下沉设计决策

## 背景

Pack模块已完成 OOP 化迁移和 PackRequest 声明式 API 设计，但 picture_process.cpp 仍然是唯一绕过
`pack::execute(PackRequest)` 公共入口的调用方。它直接依赖 `pack::detail::` 类型、`pack::internal::` 函数、
`Packer` 类，并手工构造 `PackPlan`。

## 现状：picture_process.cpp 的泄漏清单

| 行为 | 应归属 |
|------|--------|
| 构造 `pack::detail::PackEntryInput` 向量 | pack内部 |
| 手工命名冲突检测+碰撞命名 (`planPictureZipEntryNames`) | pack内部 |
| 手工逻辑分组 (`PictureLogicalBucket`, `buildPictureLogicalParts`) | pack内部 |
| 直接调 `Packer::groupPackEntries()` | pack内部 |
| 直接调 `pack::internal::buildGroupOrdinalRanges()` | pack内部 |
| 手工构造 `PackPlan` 结构体 | pack内部 |
| 手工拼装 zip 名称 (`PicturePackNamingState`, `buildPicturePackBaseName`) | pack内部 |
| include `pack/packer_types.h` + `pack/packer.h` + `pack/pack_internal.h` | 不应暴露 |

## 共识决策

### D-FS-01: Summary 功能下沉为 pack 模块能力开关

Picture 的 summary 功能（按源目录选一张图放在 zip 最前面）下沉为 pack 模块的能力开关。
调用方最多提供自定义前缀（summary 前缀和普通图片前缀），不提供则用内部默认实现。
唯一保证：summary 图片在 zip 内排在最前。

### D-FS-02: 分组策略由调用方指定，pack 实现

调用方只需声明分组规则（如"按源目录保持亲和性，最多2000张/组"），
具体的 `PictureLogicalBucket` 构建、逻辑分组→物理分组转换全部内化到 pack 模块。

### D-FS-03: PackPlan 退化为纯内部类型

`PackPlan` 不再对外暴露。以下字段全部由 pack 模块内部解析或使用默认值：

- `maxParallelJobs` — 内部分辨或使用默认并发数
- `compact` — 内部默认
- `removeOnFailure` — 内部默认
- `progressCallbacks` — 内部管理（可恢复执行时自动设置）
- `progressLabelForIndex` — 内部生成
- `groups` — 内部分组构建

### D-FS-04: 调用方唯一入口 = `pack::execute(PackRequest)`

调用方只通过 `PackRequest` 表达：输入文件、输出目录、分组策略、命名偏好、
以及 picture 特有的 summary 开关/前缀。所有内部类型 (`detail::`, `internal::`, `Packer`, `PackPlan`)
对调用方不可见。

## 待解决

### RQ-FS-01: 命名冲突处理的统一抽象

`planPictureZipEntryNames` 实现了三种模式：Flat、Keep、flat-with-force。
需要调查这些模式能否抽象为 pack 模块的命名策略枚举 + 前缀配置，
从而完全消除调用方的手工命名处理。

详见 research questions。
