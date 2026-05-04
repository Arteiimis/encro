---
title: "PackRequest API扩展：集成summary、分组策略、命名策略"
trigger_condition: "RQ-PACK-001 命名冲突抽象方案明确，PackRequest 扩展的 API 形态确定"
planted_date: 2026-05-04
---

# PackRequest API扩展：集成summary、分组策略、命名策略

当前 `PackRequest` 已经能覆盖 video 和 pipeline (Directory) 两种场景，但 picture 模块
的特殊需求（summary 图片优先、按源目录分组亲和性、自定义命名冲突处理策略）需要
`PackRequest` 的进一步扩展才能覆盖。

## 需要扩展的能力

### 1. Summary 开关

- **字段草案:** `bool enableSummary = false` + `std::optional<std::string> summaryPrefix` + `std::optional<std::string> regularPrefix`
- **默认行为:** 无 summary，等价于当前 Media 模式的普通分组
- **开启后行为:** 每个源目录选一张图片作为 summary entry，放在打包组的最前面，使用指定前缀

### 2. 分组策略

- **字段草案:** 枚举或策略对象，表达"按源目录亲和性" vs "单纯按大小" vs 自定义
- **默认行为:** 按源目录保持同组（当前 PackRequest 的默认）
- **picture 需求:** 按源目录保持同组 + 每组最多 2000 张 + summary 优先

### 3. 命名策略

- **待 RQ-PACK-001 确定**
- 预期方向: 策略枚举 (Flat/Keep) + `forceConflictHandling` flag + 前缀配置

## 达成后效果

`picture_process.cpp` 的调用将简化为类似：

```cpp
auto const packRes = pack::execute(pack::PackRequest{
    .entries = packedFilePaths,
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .naming = pack::NamingConfig{
        .layout = config.outputLayout,
        .forceConflictHandling = config.forceNameConflictHandling,
        // TBD: 命名策略扩展
    },
    .grouping = pack::GroupingConfig{
        .strategy = pack::GroupingStrategy::SourceDirAffinity,
        .maxEntriesPerGroup = 2000,
    },
    .summary = pack::SummaryConfig{
        .enabled = config.pictureFolderSummary,
        .summaryPrefix = "0000__",
        .regularPrefix = "1000__",
    },
    .maxParallelJobs = config.maxParallelJobs,
    .jobState = ctx.runtime.jobState.get(),
});
```

Picture 模块将完全不再直接引用 `packer_types.h`, `packer.h`, `pack_internal.h`。
