# Phase 13: 分组统一 & 命名内化 — Context

**Gathered:** 2026-05-01
**Status:** Ready for planning

<domain>
## Phase Boundary

将 Picture / Video 分组策略统一为两层切分 (`groupPackEntriesWithSubparts`)，消除 `groupPackFiles`（单层）分叉。zip 文件命名和条目命名规则内化到 pack 模块，调用方通过 `NamingConfig` + optional callback 控制。删除所有泄漏到消费者的分组/命名类型和函数。

范围：SIMPLIFY-05~08。SIMPLIFY-11~14（行为保持、IPacker 移除）属于 Phase 14。
</domain>

<decisions>
## Implementation Decisions

### 分组统一 (SIMPLIFY-05)
- **D-01:** `buildMediaPackPlan` 从 `groupPackFiles`（单层）改为 `groupPackEntriesWithSubparts`（两层）；两层是超集，单层为未触发 subPart 的退化情况
- **D-02:** Directory mode 保持 `groupFilesBySize` — 无 sourceDir 概念（目录树内所有文件均摊大小），两层无收益
- **D-03:** `groupEncodedVideosForPack`（两个重载）删除 — video_output_planning.cpp:176-195，已是 no-op stub
- **D-04:** `buildPicturePackPlan` 删除 — 其功能被 pack::execute() 覆盖；picture 直接调用 execute()

### 命名内化 (SIMPLIFY-06/07)
- **D-05:** zip 文件命名模式由 mode 决定，`NamingConfig::baseName` 可选覆盖
  - Media mode: `{baseName}_part{part}[.{subPart}][ordinal].zip` （baseName 为空时 `part{}.zip`）
  - Directory mode: `{dirName}_part{index}[ordinal].zip` （dirName 自动从 dirPath.filename() 推导，忽略 baseName）
- **D-06:** `NamingConfig` 新增 `baseName` 字段：`std::optional<std::string>`；picture 传入 `dirPath.filename().string()`，video 不传（→空）
- **D-07:** zip 条目命名：默认由 `NamingConfig.layout` + `forceConflictHandling` 驱动；PackRequest 增加 optional callback `entryNameForFile` 供高级消费方覆盖（picture 注入 `planPictureZipEntryNames` 结果）
- **D-08:** `PicturePackNamingState`、`buildPicturePackBaseName` 内移到 pack.cpp 匿名命名空间，作为命名辅助函数

### 命名策略钩子 (SIMPLIFY-08)
- **D-09:** `NamingConfig` 新增 `zipNameStrategy` 预留回调：`std::function<std::string(std::size_t partIndex, std::size_t subPartIndex, std::size_t totalSubParts, std::string_view baseName, FileOrdinalRange)>`；null 时使用 mode 默认命名。当前零消费者。

### 消费者适配
- **D-10:** picture_process.cpp 非压缩路径：`runPicturePackWorkflow` 直接调用 `pack::execute(PackRequest{.entries=..., .naming=NamingConfig{.baseName=dirName, .layout=..., .forceConflictHandling=...}, .entryNameForFile=...})`
- **D-11:** `PreparedPicturePack`, `preparePicturePack`, `printPicturePackWorkflowSummary` 删除
- **D-12:** `buildPicturePackEntryInputs` 保留 — 构建 `PackEntryInput` 用于 sourceKey/sourceDir 分组控制（Phase 13 内暂不移入 pack 模块）

### 测试适配
- **D-13:** `picture_process_tests.cpp` 中 2 个 `buildPicturePackPlan` 测试用例重写为 `pack::execute()` 调用，断言匹配 execute() 输出格式
- **D-14:** 所有现有 `pack_execute_test.cpp` 测试保持绿 — 分组统一不应改变 zip 文件数量和文件内部内容

### Folded Todos
- [pack-simplify-single-entry](.planning/todos/pending/pack-simplify-single-entry.md) 的 G-2（分组统一）+ G-3（命名内化）在此阶段闭合

### the agent's Discretion
- `buildMediaPackPlan` 内部从 `groupPackFiles` → `groupPackEntriesWithSubparts` 的具体重写
- 匿名命名空间命名辅助函数在 pack.cpp 内的组织
- Picture non-compress 路径中 entryNameForFile callback 的 lambda 构造方式
- 测试断言重写后的等价性验证
</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### 分组相关代码
- `src/pack/pack.cpp:42-110` — `buildMediaPackPlan`（当前使用 groupPackFiles，需改为 groupPackEntriesWithSubparts）
- `src/pack/packer.cpp:574-659` — `groupPackEntriesWithSubparts` 实现（两层切分模型）
- `src/pack/packer.cpp:547-572` — `groupPackFiles`（单层，将被 Media 路径弃用但保留给 Directory）
- `src/pack/packer.cpp:518-545` — `groupPackEntries`（内部单层，被 subparts 调用）
- `src/pack/packer.h:48-66` — groupPackFiles / groupPackFilesWithSubparts 声明
- `src/video/video_output_planning.cpp:176-195` — `groupEncodedVideosForPack`（待删除 no-op）

### 命名相关代码
- `src/pack/pack.cpp:80-103` — 当前 Media 的 ordinal range + zipNameForIndex 构建
- `src/pack/packer.cpp:758-789` — Directory 的 naming（ordinal + `{dirName}_part{}` pattern）
- `src/picture/picture_process.cpp:69-113` — `planPictureZipEntryNames`（条目命名，保留为 callback 数据源）
- `src/picture/picture_process.cpp:148-199` — `buildPicturePackEntryInputs`（分组控制 key，保留）
- `src/picture/picture_process.cpp:225-254` — `PicturePackNamingState` + `buildPicturePackBaseName`（zip 命名，内移到 pack.cpp）
- `src/pack/pack_internal.h` — `appendOrdinalRangeSuffix`、`buildGroupOrdinalRanges`（已 internal）
- `src/pack/pack_service.cpp:343-430` — internal 命名函数实现

### pack.h 公开 API（需扩展）
- `src/pack/pack.h:4-61` — `PackRequest`, `NamingConfig`, `PackMode`, `execute()`

### 消费者（需适配）
- `src/picture/picture_process.cpp:534-592` — `buildPicturePackPlan`（待删除）
- `src/picture/picture_process.cpp:317-477` — `runPicturePackWorkflow`（简化）
- `src/picture/picture_process.cpp:479-516` — `packAllPicsToZip`（简化）
- `src/picture/picture_process.cpp:225-268` — `PicturePackNamingState`, `PreparedPicturePack`, `print...Summary`（待删除）
- `src/video/video_process.cpp:408-415` — 视频打包（已用 execute()，仅需确认无变化）

### 规范
- `.planning/codebase/CONVENTIONS.md` — designated initializer、eh::Result<T>、trailing return type、匿名命名空间
- `.planning/codebase/STACK.md` — libzippp、Catch2、xmake
</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
| Asset | Role in Phase 13 |
|-------|-----------------|
| `PackEntryPartition` | 两层分组结果载体（partIndex + subPartIndex），已在 packer_types.h 定义 |
| `groupPackEntriesWithSubparts` | 两层分组实现，已在 packer.cpp 实现，picture 已在用 |
| `FileOrdinalRange` | 文件序数范围，保留为命名辅助（pack_internal 使用） |
| `PackFileEntry` | 文件条目（sourcePath + zipEntryName），保留为内部类型 |
| `NamingConfig` | 扩展：新增 `baseName` + `zipNameStrategy` 字段 |

### Established Patterns
- **Designated initializers** — PackRequest 构建遵循此风格
- **`std::function` callback** — PackPlan 已有 `zipNameForIndex`、`progressLabelForIndex` callback 先例
- **匿名命名空间 free functions** — 内部辅助函数的标准组织方式
- **`pack::internal::` namespace** — 跨 TU 使用的内部类型/函数（`appendOrdinalRangeSuffix` 等）
- **`namespace fs = std::filesystem;`** — 文件顶部别名

### Zipped-file-name Design Space

Current naming patterns → Unified patterns (Phase 13):

| Mode | Current | Phase 13 (default) | baseName override |
|------|---------|-------------------|-------------------|
| Media (video) | `part1[1~5#5p].zip` | `part1[1~5#5p].zip` | `{baseName}_part1[1~5#5p].zip` |
| Media (picture) | `pics_part1.2[3~3#1p].zip` | `pics_part1.2[3~3#1p].zip` | — (baseName="pics") |
| Directory | `docs_part1[1~100#100p].zip` | `docs_part1[1~100#100p].zip` | — (auto) |

### Ordering of Files in Zip

Picture uses `sourceKey = "1000__" + ...` / `"0000__" + ...` in `buildPicturePackEntryInputs` 来确保：
- Summary entries (0000__) 排在 Normal entries (1000__) 前面
- 同一 sourceDir 的文件归入同一个 partition

Phase 13 中此逻辑保留在 picture 代码内（`buildPicturePackEntryInputs`），通过 `entryNameForFile` callback 注入 entry name。

### Integration Points

| Consumer | 当前调用方式 | Phase 13 后 |
|----------|------------|------------|
| `picture_process.cpp` (compress) | `pack::execute(PackRequest{.entries=paths, .mode=Media, ...})` | 不变 |
| `picture_process.cpp` (no compress) | `preparePicturePack()` → `buildPicturePackPlan()` → extract paths → `pack::execute()` | `pack::execute(PackRequest{.entries=paths, .naming=..., .entryNameForFile=...})` |
| `video_process.cpp` | `pack::execute(PackRequest{.entries=paths, .mode=Media, ...})` | 不变 |
| `pipeline.cpp` | `pack::execute(PackRequest{.entries={dir}, .mode=Directory, ...})` | 不变 |
| `video_output_planning.cpp` | `groupEncodedVideosForPack()` (no-op) | **删除调用** |
</code_context>

<specifics>
## Specific Ideas

- 两层分组 → 单层退化：`groupPackEntriesWithSubparts` 在 entries 不超限时，返回 `partIndex=1, subPartIndex=0` 的单个 partition — 等同于单层结果
- `baseName` 为空时：Media zip 命名为 `part{}.zip`（如 `part1[1~5#5p].zip`）— 与当前 behavior 一致
- `baseName` 非空时：Media zip 命名为 `{base}_part{}.zip`（如 `pics_part1[1~5#5p].zip`）— 匹配 picture 当前 behavior
- `entryNameForFile` callback 为 null 时：使用 `filePath.filename()` 作为 entry name（当前 behavior）
- `entryNameForFile` callback 非 null 时：使用 callback 返回值（picture 行为保持）
- `zipNameStrategy` hook 为 null 时：mode 默认命名逻辑（当前零消费者）
</specifics>

<deferred>
## Deferred Ideas

- folderSummary 完全内化到 pack 模块（picture 特有 summary-entry-first 排序） → 需要更深设计
- Directory mode 也使用两层 grouping → 无 sourceDir 区分场景，`groupFilesBySize` 已够用
- `planPictureZipEntryNames` 迁移到 pack 模块 → entryNameForFile callback 是最小解耦
- IPacker 移除 + MockPacker 删除 + PackService 直接持有 Packer → Phase 14
- [remove-ipacker-abstraction](.planning/todos/pending/remove-ipacker-abstraction.md) → Phase 14
</deferred>

---

*Phase: 13-grouping-naming*
*Context gathered: 2026-05-01*
