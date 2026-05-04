# Phase 18: PackPlan Pure Internalization - Context

**Gathered:** 2026-05-04
**Status:** Ready for planning

## Phase Boundary

将 PackPlan 从公开头文件移除，使 `src/pack/` 外的消费者无法引用、构造或操作 PackPlan。编译期强制封装边界验证——消费者代码中 `pack::PackPlan` 不可达。

## Requirements (SINK-04)

From `.planning/REQUIREMENTS.md`:

1. `#include "pack/pack.h"` does not expose `pack::PackPlan` — compile test confirms unreachable from public header
2. `static_assert(std::is_aggregate_v<PackPlan>)` removed — no longer needed once PackPlan is internal-only
3. All existing tests pass with zero behavioral change after PackPlan is moved to internal header

## Implementation Decisions

### Type Relocation
- **D-01:** PackPlan + PackFileEntry → 新建 `src/pack/pack_plan_internal.h`
- **D-02:** 仅 PackPlan + PackFileEntry 内移，其他类型（PackEntryInput、PackMode、GroupingStrategy、SummaryConfig、NamingConfig、NamingStrategy）保留在 `pack_types.h` 作为公开 API
- **D-03:** `pack_service.h` / `packer.h` 改为 include `pack_plan_internal.h`；`pack.h` 不再间接暴露 PackPlan

### Compile-Time Enforcement
- **D-04:** 编译期测试验证——消费者代码中引用 `pack::PackPlan` 触发编译错误（static_assert / SFINAE 验证）

### the agent's Discretion
- 编译期测试的具体实现方式（编译失败断言 vs 单独测试文件）
- `pack.h` / `pack_types.h` 的具体 include 结构调整
- 是否移除 `static_assert(is_aggregate_v)` 以及移除时机

## Canonical References

- `.planning/PROJECT.md` — v1.5 milestone definition, architecture overview, out-of-scope decisions (PackPlan stays aggregate)
- `.planning/REQUIREMENTS.md` — SINK-04 requirement with 3 success criteria
- `src/pack/pack.h` — current public header (PackRequest, PackMode, NamingConfig, execute() declaration)
- `src/pack/pack_types.h` — current location of PackPlan + PackFileEntry (to be split)
- `src/pack/pack_service.h` — PackService internal header, primary PackPlan consumer
- `src/pack/packer.h` — Packer internal header, PackPlan consumer
- `src/pack/pack_internal.h` — existing internal namespace, reference for internal header pattern

## Existing Code Insights

### Reusable Assets
- `pack_internal.h` 已存在——内部头文件的命名和组织模式可复用
- `pack_types.h` 已有公开/内部类型混放——拆分边界清晰

### Established Patterns
- PackPlan 作为 aggregate（16 处 designated initializer）——保持不动
- 头文件组织：公开头 `pack.h` include 类型头 `pack_types.h`，内部头独立 include
- 消费者通过 `pack::execute(PackRequest)` 交互——PackPlan 仅内部可见

### Integration Points
- `pack.h` — 公共 API 入口，需确保不再间接暴露 PackPlan
- `packer_tests.cpp` / `pack_service_tests.cpp` — 内部测试，可 include `pack_plan_internal.h`
- 消费者测试（picture/video/pipeline）——应验证无法引用 PackPlan

## Specific Ideas

- 编译期测试可采用负测试（negative compilation test）模式：单独测试文件 include `pack/pack.h` 后尝试使用 `pack::PackPlan`，标记为预期编译失败

## Deferred Ideas

None — discussion stayed within phase scope

---

*Phase: 18-PackPlan Pure Internalization*
*Context gathered: 2026-05-04*
