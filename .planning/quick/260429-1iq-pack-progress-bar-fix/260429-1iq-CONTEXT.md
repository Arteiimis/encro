# Quick Task 260429-1iq: 打包进度条显示错误修复 - Context

**Gathered:** 2026-04-28
**Status:** Ready for planning

<domain>
## Task Boundary

打包时进度条显示的正在打包指示错误，一直在跳变，包括总待打包数也在跳变，正确逻辑应该是显示：`<已打包包数>/<总包数> [<已打包文件数>/<总文件数>]`，分析下问题根因然后修复。

**根因已定位：** `pack_service.cpp:packGroups()` 第223-269行，紧凑模式下单进度条被多个并行归档任务的回调交替更新，每个回调显示的是各自归档的 `index+1 / archiveCount` 和 `fileIndex / fileCount`（归档内部计数），而非全局累计进度。
</domain>

<decisions>
## Implementation Decisions

### 归档完成判定时机
- 以 `packFilesToZip` 返回为准（文件写入完成且zip已关闭）。compact 模式下 packFilesToZip 内部直接关闭 zip，无非 compact 模式的 Finalizing 步骤，返回时即算完成。

### 文件计数粒度
- 保持 `entries.size()`（含所有条目，不区分是否为 regular file）。与当前 `packFilesToZip` 回调中的 `totalCount` 一致，简单稳定。

### 已完成归档数的原子递增时机
- 在 `packFilesToZip` 成功返回后，在外层 task lambda 中递增 `completedArchiveCount`（配合 `std::atomic` + mutex 保护进度条文本更新）。
- 完成后更新进度条文本为累计 `completedArchiveCount / archiveCount` + `completedFileCount / compactTotalFiles`。

### 进度文本格式
- 保持现有风格：`Packing: archive X/Y [file A/B]`
- X = completedArchiveCount（已完成归档数）
- Y = archiveCount（总归档数）
- A = completedFileCount（已完成文件总数）
- B = compactTotalFiles（文件总数）

全部完成后显示：`Packed: archive N/N complete`（保持现有格式不变）

### Agent 裁量
- 新增 `std::atomic<std::size_t> completedArchiveCount` 跟踪已完成的归档数
- 修改 compact 进度回调：不再使用 `index + 1 / archiveCount` 和 `fileIndex / fileCount`，改为使用 `completedArchiveCount / archiveCount` 和 `completedFileCount / compactTotalFiles`
- 在归档完成时（`zippedFiles[index] = zipPath` 之后）递增 `completedArchiveCount` 并更新进度条文本
- 回调中 archive 计数和 file 计数分别独立加锁/原子操作，避免不同粒度的竞争
- 只需修改 `pack_service.cpp` 中的 `packGroups()` 函数
</decisions>

<specifics>
## Specific Ideas

用户指定的正确格式：`<已打包包数>/<总包数> [<已打包文件数>/<总文件数>]`，映射为 `Packing: archive X/Y [file A/B]`。
</specifics>

<canonical_refs>
No external specs — requirements fully captured in decisions above.
</canonical_refs>
