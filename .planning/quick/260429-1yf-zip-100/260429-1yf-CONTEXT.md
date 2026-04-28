# Quick Task 260429-1yf: 打包进度条未计入zip关闭耗时 - Context

**Gathered:** 2026-04-28
**Status:** Ready for planning

<domain>
## Task Boundary

打包时的进度条计算只计算已打包文件数，未含zip压缩包关闭保存耗时，导致进度条满100%时压缩包尚未完全写入磁盘。

**根因：** `packer.cpp:441-473` compact 模式的 `packFilesToZip` 中，回调 `onEntryPacked` 在每文件加入zip内存缓冲后触发，`zip.close()`（真正写盘）在所有回调之后才执行。进度条在末次回调时已达100%，但磁盘写入尚未开始。对比完整进度模式（packer.cpp:379-439）在 `zip.close()` 期间有 "Finalizing" 旋转指示器。
</domain>

<decisions>
## Implementation Decisions

### Finalizing 指示方式
- 使用旋转指示器 `/ - \ |`，与完整进度模式（`runFinalizingSpinner`）保持一致。
- 在 compact 模式进度条文本后追加 `| Finalizing /` 旋转帧，120ms 间隔。

### 进度百分比上限
- 保持 100%。文件全部添加后进度条到 100%，同时显示 "Finalizing" 旋转指示器。与完整进度模式行为一致。

### 实现方式
- 给 compact 版 `packFilesToZip` 新增 `std::atomic<bool>* finalizing` 参数。
- `packFilesToZip` 在 `zip.close()` 前设 `*finalizing = true`，`zip.close()` 后设 `*finalizing = false`。
- `packGroups` 中使用共享的 `std::atomic<std::size_t> finalizingCount` 跟踪正在 finalizing 的归档数。
- 在 `packGroups` 中启动单个 spinner jthread，当 `finalizingCount > 0` 时更新进度条文本。
- 进度条文本格式：`Packing: archive X/Y [file A/B] | Finalizing /`

### Agent 裁量
- 是否复用已有 `runFinalizingSpinner` 函数或写新的 compact 版本 — agent 决定
- Spinner 线程管理：在 `packGroups` 中创建，在函数返回前 join
- 线程安全：`finalizingCount` 用 `std::atomic`，进度条文本更新加 `compactProgressMutex`
</decisions>

<specifics>
## Specific Ideas

与完整进度模式的 finalizing 行为保持一致：100% 进度 + 旋转指示器 `"| Finalizing /"`。
</specifics>

<canonical_refs>
No external specs — requirements fully captured in decisions above.
</canonical_refs>
