# Research Questions

> Append-only log. Add new questions at the bottom with date and context.
> When a question is resolved, mark as `[RESOLVED]` with a brief answer and date.

---

## 2026-05-04

### RQ-PACK-001: 命名冲突处理的统一抽象

**Context:** `/gsd-explore` — Picture 模块的 `planPictureZipEntryNames` 手工实现了三种
命名冲突处理模式：Flat (1000__前缀, 无冲突直接使用文件名, 有冲突打hash)、
Keep (保持原始相对路径)、flat-with-force (强制冲突命名，即使没有命名冲突也打hash)。

Pack 模块自身的 Directory 模式已有 `forceConflictHandling` 支持冲突命名
（通过 `collisionnaming::buildConflictHandledFlatName`），但 Picture 的三种模式
没有与 pack 模块的命名机制统一。

**Question:** Picture 的 Flat / Keep / flat-with-force 三种模式能否抽象为 pack 模块
的命名策略枚举 + 前缀配置，从而消除 `planPictureZipEntryNames` 的手工实现？

**Investigation scope:**
- Flat 模式与 pack 现有 `forceConflictHandling=false` 的映射关系
- flat-with-force 与 pack 现有 `forceConflictHandling=true` 的映射关系
- Keep 模式是否需要新的策略值
- 前缀方案 (0000__summary / 1000__regular) 的配置化入口设计
- `collisionnaming` 命名空间的使用是否可以完全内化到 pack 模块

**Status:** Open
