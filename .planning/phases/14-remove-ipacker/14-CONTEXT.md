# Phase 14: IPacker 抽象层移除 & 验证 — Context

**Gathered:** 2026-05-01
**Status:** Implemented (backfill); 3 blocking issues from MILESTONE-AUDIT.md

<domain>
## Phase Boundary

移除 v1.3 引入的 `IPacker` 抽象基类，Packer 回归为唯一生产实现的 `final` 类。`PackService` 从 `unique_ptr<IPacker>` 改为直接持有 `Packer` 值成员。删除 `MockPacker` 测试替身，所有 mock 测试改写为使用真实 `Packer` + `TempDir` 的集成测试。

范围：SIMPLIFY-15~17。SIMPLIFY-11（所有断言绿）、SIMPLIFY-13（resumable jobState）、SIMPLIFY-14（ZIP 条目冲突处理）在此阶段验证保持。
</domain>

<decisions>
## Implementation Decisions

### 移除 IPacker 抽象基类 (SIMPLIFY-15)
- **D-01:** 删除 `src/pack/ipacker.h`（47 行，3 纯虚方法声明）
- **D-02:** `Packer` 移除 `: public IPacker` 继承，保持 `final` 不变 — 回归为无继承的独立 final 类
- **D-03:** `packFilesToZip`（2 重载）和 `buildDirectoryPackPlan` 上移除 3 个 `override` 关键字
- **D-04:** `packer.h` 中 `#include "pack/ipacker.h"` 替换为单行注释 `// IPacker abstraction removed — Packer is the sole concrete implementation`
- **D-05:** `packer.cpp` 中无签名变更 — 方法签名在 `.h` 上已移除 `override`，`.cpp` 定义无该关键字

### PackService 直接持有 Packer (SIMPLIFY-16)
- **D-06:** `PackService` 成员从 `std::unique_ptr<IPacker> packer_` 改为 `Packer packer_` — 值语义，Packer 自身零堆分配
- **D-07:** 构造函数从 `explicit PackService(std::unique_ptr<IPacker>)` 改为 `PackService() = default` — 无参数，Packer 默认构造
- **D-08:** 移除 `pack_service.h` 中 `class IPacker;` 前向声明和 `#include <memory>`
- **D-09:** 新增 `#include "pack/packer.h"` — Packer 现在是完整类型，必须可见
- **D-10:** 所有 `packer_->method()` 调用改为 `packer_.method()` — 从指针解引用改为成员访问
- **D-11:** 移除 `pack_service.cpp` 中 `#include "pack/packer.h"` — 已在 `.h` 中包含

### 构造点简化
- **D-12:** `pack.cpp` 中 4 处 `std::make_unique<Packer>()` + `PackService(std::move(packer))` 构造全部替换为 `PackService svc;`（默认构造）
  1. `runNonResumable()` (行 41): `PackService svc` 替换 `auto packer = std::make_unique<Packer>()` + `PackService svc(std::move(packer))`
  2. `buildMediaPackPlan()` (行 129): `Packer packer;` 替换 `auto packer = std::make_unique<Packer>()`，后续 `packer->groupPack...` 改为 `packer.groupPack...`
  3. `runResumable()` (行 280): `PackService svc2` 替换 `auto packer2 = std::make_unique<Packer>()` + `PackService svc2(std::move(packer2))`
  4. `execute()` (行 309): `Packer packer;` 替换 `auto packer = std::make_unique<Packer>()`，`packer->buildDirectoryPackPlan` 改为 `packer.buildDirectoryPackPlan`
- **D-13:** `pack_service_tests.cpp`: 全局 `testService` 从 `auto testPacker = std::make_unique<pack::Packer>(); PackService testService(std::move(testPacker));` 改为 `PackService testService;`，移除 `#include "pack/packer.h"`
- **D-14:** `packer_tests.cpp` 中 3 处 `auto p = std::make_unique<pack::Packer>(); PackService s(std::move(p));` 全部改为 `PackService s;`

### MockPacker 删除 & 测试改写 (SIMPLIFY-17, SIMPLIFY-11)
- **D-15:** 删除 `tests/packer_mock.h`（98 行） — 包含 `MockPacker : IPacker`、`PackFilesToZipCall`、`BuildPlanCall` 捕获记录类型
- **D-16:** `pack_service_mock_tests.cpp` 中 10 个 mock 测试（`[pack-service][mock]`）改写为 10 个集成测试（`[pack-service]`）：
  - 移除 `#include "packer_mock.h"`，新增 `#include "test_utils.h"` + `#include <libzippp/libzippp.h>` + helpers
  - 每个测试使用 `TempDir` 创建真实磁盘文件，构造真实 `PackService`，通过 `testutils::listZipRegularEntryNames()` 验证 zip 内容
  - 测试覆盖映射表：

  | 原 Mock 测试 | 改写后集成测试 | 覆盖行为 |
  |---|---|---|
  | `packGroups delegates to IPacker::packFilesToZip for each group` | `packAllFilesInDirectory packs files from a real directory` | PackService 编排 → 真实 zip |
  | `packGroups passes correct zip paths` | `packGroups creates zip at correct output path` | zip 输出路径正确 |
  | `packGroups propagates IPacker errors` | `packAllFilesInDirectory returns error for non-existent directory` | 错误传播（真实错误） |
  | `packGroups returns zipped file paths` | `packGroups returns correct zipped file paths` | 多 zip 路径返回 |
  | `packGroups empty plan returns empty` | `packGroups empty plan returns empty` | 空计划正确返回 |
  | `packGroups compact mode calls compact overload` | `packGroups compact mode writes correct zip content` | compact 模式真实 zip 输出 |
  | `packGroups full-progress mode calls full overload` | `packGroups full-progress mode writes correct zip content` | full-progress 模式真实 zip 输出 |
  | `packGroups compact mode passes finalizingCount pointer` | `packGroups handles non-existent source files gracefully` | 坏源文件优雅处理 |
  | `packAllFilesInDirectory delegates to buildDirectoryPackPlan` | `packAllFilesInDirectory respects non-recursive flag` | 非递归 flag → 真实 zip |
  | `packAllFilesInDirectory propagates buildPlan error` | `packAllFilesInDirectory with forceNameConflictHandling` | forceConflictHandling → 真实 zip |
  
  - 净效果：mock 测试 230 行 → 集成测试 309 行（+79 行，含辅助函数和扩展断言）

### 行为保持验证 (SIMPLIFY-11, SIMPLIFY-13, SIMPLIFY-14)
- **D-17:** 所有 945 断言在改写后保持绿 — 改写测试通过真实 Packer 验证，覆盖不减
- **D-18:** Resumable 执行（`jobState` / `selectPackPlanIndexes`）逻辑不变 — 仅构造函数调用简化，内部逻辑零改动
- **D-19:** ZIP 条目冲突处理（`uniqueifyZipEntryNames` / `forceNameConflictHandling`）逻辑不变 — Packer 为同一实例，方法实现零改动

### Folded Todos
- [remove-ipacker-abstraction](.planning/todos/pending/remove-ipacker-abstraction.md) — "合并 Packer 与 IPacker，移除抽象层"（resolves_phase: 14）

### the agent's Discretion
- PackService 采用 `Packer packer_` 值语义（非 `unique_ptr<Packer>`）— Packer 无堆分配开销，零复制问题，生命周期完整匹配
- 改写测试的断言从"MockPacker 是否被带正确参数调用"转为"真实 zip 是否含预期内容" — 覆盖更全面，断言语义更强
- `pack_service_mock_tests.cpp` 保留原文件名不变 — 文件内容已不再是 mock，但重命名会增加追踪难度
</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### 文件变更清单

| 文件 | 变更类型 | 行数变化 |
|------|---------|----------|
| `src/pack/ipacker.h` | **删除** | -47 |
| `src/pack/packer.h` | 修改（移除继承 + override） | -6 / +1 注释 |
| `src/pack/packer.cpp` | 无变更（签名无 override 关键字在 .cpp 中） | 0 |
| `src/pack/pack_service.h` | 修改（值语义、移除前向声明、移除 memory） | -6 / +2 |
| `src/pack/pack_service.cpp` | 修改（ptr→.member、移除 #include packer.h） | -4 / +0 |
| `src/pack/pack.cpp` | 修改（移除 make_unique 包装 4 处） | -6 / +4 |
| `tests/packer_mock.h` | **删除** | -98 |
| `tests/pack_service_mock_tests.cpp` | 重写（mock → 集成测试 10 个） | -230 / +309 |
| `tests/pack_service_tests.cpp` | 修改（简化全局 testService） | -3 / +1 |
| `tests/packer_tests.cpp` | 修改（简化 3 处构造调用） | -6 / +3 |

**总计:** 9 文件，+240 / -319 行

### 删除的内容
- `IPacker` 抽象基类（3 纯虚方法：`packFilesToZip` × 2, `buildDirectoryPackPlan`）
- `MockPacker` 完整实现（98 行，含 `PackFilesToZipCall` + `BuildPlanCall` 结构体）
- 所有 `make_unique<Packer>()` 构造包装（7 处）
- 所有 `unique_ptr<IPacker>` 间接层
- `pack_service.h` 对 `IPacker` 的前向声明

### 变更前 vs 变更后

```
变更前:                             变更后:
┌──────────┐                       ┌──────────┐
│ IPacker  │ (虚基类, 3 纯虚方法)   │ (删除)    │
├──────────┤                       └──────────┘
│  ▲        │
│  │ 继承    │                      ┌──────────┐
│  │        │                      │ Packer   │ (final, 独立类)
├──────────┤ ┌────────────────┐    └──────────┘
│ Packer   │ │ MockPacker     │          │
│ (final)  │ │ (测试替身)     │          │ 值成员
│          │ │                │          ▼
└──────────┘ └────────────────┘    ┌─────────────┐
                          │        │ PackService │
        unique_ptr<IPacker>│        │ packer_ :   │
                          ▼        │   Packer    │
                    ┌─────────────┐ └─────────────┘
                    │ PackService │
                    │ packer_ :   │
                    │ unique_ptr  │
                    │  <IPacker>  │
                    └─────────────┘
```

### 设计文档
- `.planning/notes/remove-ipacker-abstraction.md` — 移除 IPacker 的动机与决策分析（评估零多态、零未来后端、签名维护税）
- `.planning/todos/pending/remove-ipacker-abstraction.md` — 4 个子目标（G-1~G-4）及影响文件清单
- `.planning/REQUIREMENTS.md` — SIMPLIFY-15~17 (+ SIMPLIFY-11,13,14 验证)
- `.planning/PROJECT.md` §Current Milestone — v1.4 目标（移除不必要的抽象层）
- `.planning/MILESTONE-AUDIT.md` — B-3 blocker（Phase 14 无 artifacts）

### 规范
- `.planning/codebase/CONVENTIONS.md` — designated initializer、eh::Result<T>、final class
- `.planning/codebase/STACK.md` — libzippp、Catch2、boost::program_options
</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
| Asset | Role in Phase 14 |
|-------|-----------------|
| `Packer` | 唯一生产实现，所有 zip I/O + 分组逻辑不变 |
| `PackService` | 编排层，内部调用从 `packer_->method()` 改为 `packer_.method()` |
| `PackPlan` | 无变更 — aggregate 类型，传递路径不变 |
| `PackRunResult` | 无变更 — 返回值语义不变 |
| `TempDir` | 测试工具类，10 个 mock 测试改写后所有集成测试使用它 |
| `testutils::listZipRegularEntryNames()` | libzippp 辅助函数，用于 assert zip 内容 |
| `kDefaultMaxArchiveGroupSize` | 500 MB 常量，无变更 |

### Established Patterns
- **`final` class** — Packer 自 v1.3 起即为 `final`，移除 IPacker 后 `final` 保留（遵循 Core Guidelines C.35）
- **值语义依赖注入** — `Packer packer_` 成员直接持有，无智能指针间接层
- **`= default` 构造函数** — PackService 零参数构造，Packer 自身也是默认构造
- **Designated initializers** — PackPlan 构建不变
- **命名空间级自由函数** — `pack::execute()` 内部管理的 Packer 栈分配（`Packer packer;`）
- **匿名命名空间辅助函数** — 内部实现组织方式不变

### Integration Points

| Consumer | Phase 14 前 | Phase 14 后 |
|----------|------------|------------|
| `pack.cpp` execute() | `auto packer = make_unique<Packer>(); Packer*->method()` | `Packer packer; packer.method()` |
| `pack.cpp` runNonResumable() | `make_unique<Packer>()` + move → PackService | `PackService svc;` |
| `pack.cpp` runResumable() | `make_unique<Packer>()` + move → PackService | `PackService svc2;` |
| `pack_service.cpp` packGroupsCompact/Full | `packer_->packFilesToZip(...)` | `packer_.packFilesToZip(...)` |
| `pack_service.cpp` packAllFilesInDirectory | `packer_->buildDirectoryPackPlan(...)` | `packer_.buildDirectoryPackPlan(...)` |
| `pack_service.cpp` runDirectoryPackWorkflow | `packer_->buildDirectoryPackPlan(...)` | `packer_.buildDirectoryPackPlan(...)` |
| `pack_service_tests.cpp` global testService | `make_unique<Packer>()` → PackService | `PackService testService;` |
| `packer_tests.cpp` 3 测试 | `make_unique<Packer>()` → PackService | `PackService s;` |
| `pack_service_mock_tests.cpp` 10 测试 | `MockPacker` + `unique_ptr<IPacker>` | `PackService svc;` + TempDir |

### 未被修改的内容
- `Packer::packFilesToZip` 两重载的实现体（`packer.cpp`）— 零逻辑变更
- `Packer::buildDirectoryPackPlan` 实现体 — 零逻辑变更
- `Packer::groupPackEntriesWithSubparts` 及其所有分组方法 — 零逻辑变更
- `Packer::uniqueifyZipEntryNames` / `forceNameConflictHandling` — 零逻辑变更
- `PackService::runPackPlan` — 零逻辑变更
- `pack::execute()` 的恢复性执行逻辑 — 零逻辑变更
- `pack.h`（PackRequest / NamingConfig / execute）— 零接口变更
- `picture_process.cpp`、`video_process.cpp`、`pipeline.cpp` — 消费者零变更（已通过 execute() 使用）
</code_context>

<specifics>
## Specific Ideas

无 — 所有决策在 `.planning/notes/remove-ipacker-abstraction.md` 探索阶段已完成，本阶段仅执行。
</specifics>

<deferred>
## Deferred Ideas

无 — 本阶段是 v1.4 的最后一个实现阶段。IPacker 移除后，pack 子系统达到最小设计：
- 仅一个公开头文件 `pack.h`
- 两个内部 `final` 类：`Packer`（zip I/O + 分组）和 `PackService`（编排）
- 零抽象层，零虚函数，零智能指针间接层
</deferred>

---

*Phase: 14-remove-ipacker*
*Context backfilled: 2026-05-01*
