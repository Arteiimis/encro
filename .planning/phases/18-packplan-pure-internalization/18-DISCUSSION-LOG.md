# Phase 18: PackPlan Pure Internalization - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-04
**Phase:** 18-PackPlan Pure Internalization
**Areas discussed:** Type Relocation, PackFileEntry Scope, Compile-Time Enforcement

---
## Type Relocation

| Option | Description | Selected |
|--------|-------------|----------|
| 新建 pack_plan_internal.h | PackPlan 独占内部头文件，清晰职责分离，复用现有 internal header 模式 | ✓ |
| 移入 pack_service.h | PackPlan 直接定义在 PackService 声明中 | |
| 移入 pack_internal.h | 与现有内部符号混放 | |

**User's choice:** 新建 pack_plan_internal.h
**Notes:** 最清晰的职责分离方案，pack_service.h/packer.h 直接 include。

---

## PackFileEntry Scope

| Option | Description | Selected |
|--------|-------------|----------|
| 仅 PackPlan 内移 | 其他类型保留在 pack_types.h 作为公开 API | |
| PackPlan + PackFileEntry 内移 | PackFileEntry 也是内部分组输出，消费者不直接构造 | ✓ |

**User's choice:** PackFileEntry 随 PackPlan 一起内移
**Notes:** PackFileEntry 仅被 pack_service.cpp/packer.cpp 内部使用，PackRequest 不引用它。与 PackPlan 一起移到 pack_plan_internal.h 更干净——公开类型列表缩小，封装边界更准确。

---

## Compile-Time Enforcement

| Option | Description | Selected |
|--------|-------------|----------|
| 编译期测试 + 路径审计 | 双重保障——test 中编译失败断言 + xmake.lua include 路径检查 | |
| 仅编译期测试 | 静态断言验证消费者代码中 PackPlan 不可达 | ✓ |
| 仅路径审计 | 依赖 include 路径纪律 | |

**User's choice:** 仅编译期测试
**Notes:** 编译期测试足以验证——消费者代码 include pack.h 后引用 pack::PackPlan 应触发编译错误。

---

## the agent's Discretion

- 编译期测试的具体实现方式（编译失败断言 vs 单独测试文件）
- pack.h / pack_types.h 的具体 include 结构调整
- static_assert(is_aggregate_v) 的移除时机

## Deferred Ideas

None — discussion stayed within phase scope
