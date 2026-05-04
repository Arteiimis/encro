---
title: "Picture模块：消除对pack内部类型的直接依赖"
date: 2026-05-04
priority: high
area: pack / picture
resolves_phase: 17
---

# Picture模块：消除对pack内部类型的直接依赖

将 `picture_process.cpp` 从当前绕过 `pack::execute(PackRequest)` 公共 API 的调用方式，
全面迁移到单一入口 `pack::execute(PackRequest)`。

## 目标

### G-1: 消除 detail 类型依赖

移除 `#include "pack/packer_types.h"`，消除所有 `pack::detail::PackEntryInput` 的直接使用。
Picture 模块不再构造或持有 `pack::detail::` 类型。

### G-2: 消除 Packer 类依赖

移除 `#include "pack/packer.h"`，消除 `Packer` 实例的构造和 `groupPackEntries()` 的直接调用。
分组逻辑完全由 pack 模块内部处理。

### G-3: 消除 internal 依赖

移除 `#include "pack/pack_internal.h"`，消除 `pack::internal::buildGroupOrdinalRanges()`
和 `pack::internal::appendOrdinalRangeSuffix()` 的直接调用。

### G-4: 消除手工 PackPlan 构造

`buildPicturePackPlan()` 不再手工组装 `PackPlan`，改为构造 `PackRequest` 并调用
`pack::execute(PackRequest)`。

### G-5: Picture 特有需求通过 PackRequest API 扩展表达

- Summary 开关 + 可选前缀 → PackRequest 或 NamingConfig 的新字段
- 分组策略声明 → PackRequest 的新字段
- 命名偏好 → NamingConfig 的新字段（待 RQ-PACK-001 解决）

## 依赖

- **RQ-PACK-001** (命名冲突抽象) — 需要在设计 PackRequest 命名策略扩展方案后确认 API 形态

## 验证

- Picture 所有测试保持通过
- Picture 模块 `#include` 中不再出现 `pack/packer_types.h`, `pack/packer.h`, `pack/pack_internal.h`
- Picture 模块不再出现 `pack::detail::`, `pack::internal::` 命名空间引用
