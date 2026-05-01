# Phase 13: 分组统一 & 命名内化 — Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-01
**Phase:** 13-grouping-naming
**Areas discussed:** 分组策略统一, zip 文件命名内化, zip 条目命名内化, 消费者清理, 命名策略钩子

---

## 分组策略统一 (SIMPLIFY-05)

### buildMediaPackPlan 的 grouping 方法

| Option | Description | Selected |
|--------|-------------|----------|
| A: groupPackEntriesWithSubparts (two-layer) | 使用 PackEntryPartition 两层切分，统一 picture/video 路径 | ✓ |
| B: groupPackFiles (single-layer, 现状) | 按 sourceDir 分组 + 大小限制，无 subPart 切分 | |
| C: groupPackEntries (single-layer) | 单层但保留 sourceKey 控制能力 | |

| Option (Directory mode) | Description | Selected |
|--------|-------------|----------|
| A: groupFilesBySize (保持) | Directory 无 sourceDir 概念，纯 size bin-packing | ✓ |
| B: groupPackEntriesWithSubparts | 统一所有路径 | |
| C: groupPackFiles | 中等复杂度 | |

### groupEncodedVideosForPack 处理

| Option | Description | Selected |
|--------|-------------|----------|
| A: 删除两个重载 | 已是 no-op stub（Phase 12 注释为"forward compat, will be removed in Phase 13"） | ✓ |
| B: 迁移到 pack 模块 | 作为内部辅助 | |
| C: 保留 | 保持现状 | |

### buildPicturePackPlan 处理

| Option | Description | Selected |
|--------|-------------|----------|
| A: 删除，picture 直接调用 pack::execute() | 两阶段 grouping（picture 先做 → execute() 再做）消除 | ✓ |
| B: 保留但简化 | 仅去掉重复的 execute() 调用 | |
| C: 完整迁移到 pack 模块 | 包括 summary entry 逻辑 | |

Note: buildPicturePackPlan 当前被 `preparePicturePack` 调用，后者又被 `runPicturePackWorkflow`（非压缩路径）和 `packAllPicsToZip` 使用。删除后 picture 直接调用 `pack::execute()`，grouping + naming 由 pack 模块完成。

---

## zip 文件命名内化 (SIMPLIFY-06)

### 命名模式统一

| Option | Description | Selected |
|--------|-------------|----------|
| A: Mode-based defaults | Media: `{baseName}_part{part}[.{subPart}][ordinal].zip`; Directory: `{dirName}_part{index}[ordinal].zip` | ✓ |
| B: Single unified pattern | 所有 mode 使用统一 `{baseName}_part{part}.{subPart}[ordinal].zip` | |
| C: 保持三种不同 pattern | 仅移动代码位置，不改命名逻辑 | |

| Option (baseName 来源) | Description | Selected |
|--------|-------------|----------|
| A: NamingConfig::baseName 可选字段 | 消费者设置（picture 设 dirName，video 不设→空） | ✓ |
| B: 自动推导 | 从 entries 的 common parent 推导 | |
| C: PackRequest 顶层字段 | 放在 PackRequest 而非 NamingConfig 内 | |

### 命名逻辑位置

| Option | Description | Selected |
|--------|-------------|----------|
| A: pack.cpp 内 buildMediaPackPlan / 匿名空间 free function | 与 execute() 同文件，单一职责 | ✓ |
| B: pack_internal.h namespaced function | 暴露给 pack_service.cpp | |
| C: packer.cpp Packer 私有方法 | 放在 Packer 类中 | |

---

## zip 条目命名内化 (SIMPLIFY-07)

### 条目命名控制

| Option | Description | Selected |
|--------|-------------|----------|
| A: NamingConfig 驱动 + optional callback | NamingConfig.layout + forceConflictHandling 控制默认命名；PackRequest.entryNameForFile callback 为高级覆盖 | ✓ |
| B: 纯 NamingConfig 驱动 | 完全由 layout/forceConflictHandling 决定，无 callback | |
| C: 纯 callback 驱动 | NamingConfig 不参与条目命名 | |

| Option (entryNameForFile callback) | Description | Selected |
|--------|-------------|----------|
| A: PackRequest 的 optional callback 字段 | `std::function<std::string(fs::path const&)> entryNameForFile`; null = NamingConfig 驱动默认命名 | ✓ |
| B: NamingConfig 的子字段 | 放在 NamingConfig 内作为可选回调 | |
| C: 不提供 callback | 仅靠 NamingConfig layout + conflictHandling | |

### planPictureZipEntryNames 处理

| Option | Description | Selected |
|--------|-------------|----------|
| A: 保留在 picture 代码，通过 entryNameForFile 注入 | Picture 自行计算 entry name map，通过 callback 传给 execute() | ✓ |
| B: 迁移到 pack 模块 | 将 picture entry naming 内化为 pack 默认行为 | |
| C: 删除，完全依赖 NamingConfig | picture 省略所有 entry naming 逻辑 | |

Rationale: picture 的 `1000__` 前缀 + folderSummary (`0000__` 前缀) 是 picture 特有的排序/分组控制机制，不应硬编码进 pack 模块。entryNameForFile callback 是干净的关注点分离。

---

## 命名策略钩子 (SIMPLIFY-08)

### 钩子设计

| Option | Description | Selected |
|--------|-------------|----------|
| A: NamingConfig::zipNameStrategy (std::function) | `(partIndex, subPartIndex, ordinalRange, baseName) -> string` 覆盖默认 zip 文件命名 | ✓ |
| B: enum + 预置策略 | 枚举命名策略，不支持自定义回调 | |
| C: 完全延后 | 等有消费者时再设计 | |

Note: 当前零消费者使用，钩子为预留（reserved）。std::function 提供最大灵活性，符合"预留"语义。

### 钩子签名

```cpp
std::function<std::string(
    std::size_t partIndex,       // 1-based
    std::size_t subPartIndex,     // 0-based
    std::size_t totalSubParts,    // 当前 part 的 subPart 总数
    std::string_view baseName,    // NamingConfig::baseName 或空
    FileOrdinalRange ordinalRange // 文件序数范围
)> zipNameStrategy;
```

当 `zipNameStrategy` 为 null 时，使用 mode 对应的默认命名。

---

## 消费者清理

### picture_process.cpp

| Option (buildPicturePackPlan) | Description | Selected |
|--------|-------------|----------|
| A: 完全删除 | 包括 PicturePackNamingState, buildPicturePackBaseName, PreparedPicturePack, preparePicturePack, printPicturePackWorkflowSummary | ✓ |
| B: 删除但保留命名状态类型 | 迁移到 pack 模块内部 | |
| C: 保留 | 仅改内部 grouping 调用 | |

| Option (picture 非压缩路径) | Description | Selected |
|--------|-------------|----------|
| A: runPicturePackWorkflow 直接调用 pack::execute() | 扫描图片后，构建 PackRequest（含 NamingConfig + entryNameForFile callback） | ✓ |
| B: 保持 preparePicturePack 包装 | 薄层保留，仅改内部实现 | |
| C: 合并为 pack::execute() 带 picture 专用参数 | | |

### video_output_planning.cpp

| Option | Description | Selected |
|--------|-------------|----------|
| A: 删除 groupEncodedVideosForPack（两个重载） | 已是 no-op，移除函数声明和定义 | ✓ |
| B: 保留声明、删除实现 | | |
| C: 保留 | | |

### tests

| Option | Description | Selected |
|--------|-------------|----------|
| A: 适配 buildPicturePackPlan 测试到 pack::execute() API | 重写 picture_process_tests 中 2 个测试用例 | ✓ |
| B: 删除 picture 分组测试 | pack_execute_test 已覆盖 Media 路径 | |
| C: 保留旧 API 测试 + 新增 execute() 测试 | | |

---

## 新增 PackRequest 字段

| 字段 | 类型 | 必要 | Description |
|------|------|------|-------------|
| entryNameForFile | `std::optional<std::function<std::string(fs::path const&)>>` | No | null = NamingConfig 驱动默认命名 |
| NamingConfig::baseName | `std::optional<std::string>` | No | null = 空（Media）或 auto（Directory） |
| NamingConfig::zipNameStrategy | `std::optional<std::function<...>>` | No | null = mode 默认 |

### 受影响的 pack.h 声明

仅 `PackRequest` 和 `NamingConfig` 结构体增加字段，`execute()` 签名不变。pack_internal.h 的 5 个函数已标记为 `namespace pack::internal`，不新增公开声明。

---

## the agent's Discretion

- `buildMediaPackPlan` 内部从 `groupPackFiles` 迁移到 `groupPackEntriesWithSubparts` 的具体实现细节
- 命名辅助函数在 pack.cpp 匿名命名空间内的组织方式
- `PicturePackNamingState` 内化后的位置（pack.cpp 匿名命名空间）
- 测试用例重写的具体断言内容（保持等价行为）
- `buildPicturePackPlan` 删除后的 include 清理

## Deferred Ideas

- folderSummary 功能完全内化到 pack 模块 → 需要更多设计（picture 特有行为）
- Entry 命名从 `1000__` 前缀迁移到通用 prefix → Phase 14+ or beyond
- Directory mode 两阶段 grouping → 当前无需求，`groupFilesBySize` 足够
- `planPictureZipEntryNames` 迁移到 pack 模块 → entryNameForFile callback 已是最小解耦
- IPacker 移除 + MockPacker 删除 → Phase 14
