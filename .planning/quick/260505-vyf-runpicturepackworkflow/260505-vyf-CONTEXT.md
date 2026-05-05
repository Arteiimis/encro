# Quick Task 260505-vyf: 优化runPicturePackWorkflow函数 - Context

**Gathered:** 2026-05-05
**Status:** Ready for planning

<domain>
## Task Boundary

优化 `picture_process.cpp` 中的 `runPicturePackWorkflow` 函数（197-463行，~265行），提升可读性，明确职责分离。两条路径（compress / non-compress）共享大量逻辑，提取重复代码，使函数退化为路由分发器。

</domain>

<decisions>
## Implementation Decisions

### Extraction Level
- 拆分为多个独立函数，`runPicturePackWorkflow` 退化为路由分发器（~15行）
- 符合项目 v1.1 lambda 可读性重构的代码风格

### PackEntryInput Building
- 提取为共享模板函数（接受图片列表和命名函数/lambda），消除两处 ~25行的重复循环
- compress 和 non-compress 路径的 source path 解析不同（压缩结果 vs 源文件），通过 callable 参数化

### Compress Path
- 提取为独立编排函数 `executeCompressPackWorkflow`
- 内部进一步拆分为子步骤函数

### PackRequest Building
- 提取 `buildPicturePackRequest` 函数，接受 PackEntryInput + 差异参数，返回 PackRequest
- 消除 ~30行重复的 designated initializer

### Function Placement
- 所有新辅助函数放在 `picture_process.cpp` 的匿名命名空间中
- 不修改任何头文件，零对外影响

</decisions>

<specifics>
## Specific Ideas

### 目标函数结构
```cpp
auto runPicturePackWorkflow(appctx::AppContext& ctx, fs::path const& dirPath)
  -> eh::Result<int> {
  auto const outputDir = ctx.config.outputPath.value_or(dirPath) / "packed";

  if (ctx.config.compressImages) {
    return executeCompressPackWorkflow(ctx, dirPath, outputDir);
  }
  return executeDirectPackWorkflow(ctx, dirPath, outputDir);
}
```

### 共享辅助函数
- `buildPackEntryInputs` — 通过 callable source resolver 参数化 source/entry 来源
- `buildPicturePackRequest` — 统一 PackRequest 构建
- `buildPackEntryInputsForSummaries` — 摘要图片的 PackEntryInput 构建（压缩和非压缩路径共享）

### 压缩路径内部拆分
- `buildCompressTasksForEntry` — 为单个图片构建 CompressTask
- `buildCompressedResultLookup` — 将压缩结果列表转为 (taskKey → compressedPath) 的查找表

</specifics>

<canonical_refs>
## Canonical References

- 项目 v1.1: lambda 可读性重构 — 10个函数提取到匿名命名空间，零头文件修改
- 项目原则: "Code clarity: no deeply nested lambdas, inline lambdas kept short and readable"
- 当前文件: `src/picture/picture_process.cpp` — 最终版 v1.5

</canonical_refs>
