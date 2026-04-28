# Quick Task 260429-1yf: 打包进度条未计入zip关闭耗时 - Summary

**Status:** complete
**Date:** 2026-04-28

## Changes

### Root Cause
`packer.cpp:441-473` compact 模式的 `packFilesToZip` 中，`onEntryPacked` 回调在所有文件加入zip内存缓冲后触发（最后一次回调时进度条达100%），之后才调用 `zip.close()` 将数据真正写入磁盘。进度条100%不代表压缩包保存完毕。相比之下，完整进度模式（packer.cpp:379-439）在 `zip.close()` 期间有 "Finalizing" 旋转指示器。

### Fix
1. `packer.h`: compact 版 `packFilesToZip` 新增 `std::atomic<std::size_t>* finalizingCount` 参数
2. `packer.cpp`: `zip.close()` 前后分别 `fetch_add(1)` / `fetch_sub(1)` 更新计数
3. `pack_service.cpp`: 
   - 新增共享 `std::atomic<std::size_t> finalizingCount` 
   - 单 spinner jthread 在 `finalizingCount > 0` 时更新进度条文本为 `"Packing: archive X/Y [file A/B] | Finalizing /"`
   - 任务完成后停止 spinner

### Files Changed
- `src/pack/packer.h` — 新增 `finalizingCount` 参数 + `<atomic>` include
- `src/pack/packer.cpp` — `zip.close()` 包裹 finalizing 标记
- `src/pack/pack_service.cpp` — spinner 线程 + 传递 `finalizingCount`

### Test Results
All 215 test cases passed (909 assertions).
