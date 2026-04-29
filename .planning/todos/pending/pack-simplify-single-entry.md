---
title: "Pack模块接口简化：PackRequest单一入口"
date: 2026-04-30
priority: high
---

# Pack模块接口简化：PackRequest单一入口

将 pack 模块从"调用方手动编排分组+命名+执行"重构为声明式单一入口，
使调用方只需描述需求，所有编排细节内化到模块内部。

## 目标

### G-1: 单一入口 API

设计 `PackRequest` 结构体（或等价输入类型），调用方通过 `pack::execute(request)` 完成所有打包。
PackPlan、buildGroupOrdinalRanges、pack::detail:: 类型不再对外暴露。

### G-2: 分组策略统一

Picture 和 Video 统一使用两层切分（groupPackEntriesWithSubparts），消除单层/双层分叉。

### G-3: 命名规则内化

zip 文件命名和条目命名均由 pack 内部完成，调用方通过命名偏好参数控制，
预留自定义钩子但当前无消费者使用。

### G-4: 配置集中注入

compact、maxParallelJobs 等配置从 AppConfig 统一注入，修复 Picture 路径
硬编码 `compact=true`（应读取 `!config.fullProgress`）。

### G-5: 行为零变化

所有现有测试保持绿。IPacker 接口保留以维持 MockPacker 测试能力。
恢复性执行（jobState）、冲突处理逻辑不变。

## 影响文件

| 文件 | 改动性质 |
|------|---------|
| `src/pack/pack_types.h` | 新增 PackRequest 类型；PackPlan 标记为内部/移除 |
| `src/pack/packer.h` | 公网面收缩；group* 方法变为 private |
| `src/pack/pack_service.h` | 新增 execute() 入口；静态方法降级为 private |
| `src/pack/packer_types.h` | pack::detail 类型不再被外部 include |
| `src/picture/picture_process.cpp` | 手动编排代码替换为 PackRequest 调用 |
| `src/video/video_process.cpp` | 手动编排代码替换为 PackRequest 调用 |
| `src/app/pipeline.cpp` | runPackOnly 简化为 PackRequest |
| `src/core/archive_plan.cpp` | 恢复性执行适配 PackRequest |
| `tests/` | PackRequest 单元测试；现有测试适配 |
