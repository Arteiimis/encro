---
title: "合并 Packer 与 IPacker，移除抽象层"
date: 2026-04-30
priority: medium
---

# 合并 Packer 与 IPacker，移除抽象层

IPacker 抽象基类经评估确认为过度设计——零生产多态、无未来后端需求、测试可改用真实集成。

## 目标

### G-1: 删除 IPacker

移除 `src/pack/ipacker.h`。Packer 不再继承任何抽象基类。

### G-2: PackService 直接持有 Packer

`PackService` 从 `unique_ptr<IPacker>` 改为持有 `Packer` 实例（或 `unique_ptr<Packer>`）。
构造点不再需要 `make_unique<Packer>()` 的间接层。

### G-3: 删除 MockPacker

移除 `tests/packer_mock.h`。`pack_service_mock_tests.cpp` 的10个测试改写为
使用真实 `Packer` + `TempDir` 的集成测试（参照 `pack_service_tests.cpp` 已有模式）。

### G-4: 测试保持绿

所有 945 断言保持绿。改写后的测试覆盖不低于原有 mock 测试。

## 影响文件

| 文件 | 改动 |
|------|------|
| `src/pack/ipacker.h` | **删除** |
| `src/pack/packer.h` | 移除 `: public IPacker` 继承；移除 `override` 关键字 |
| `src/pack/packer.cpp` | 移除 `override` 关键字 |
| `src/pack/pack_service.h` | `unique_ptr<IPacker>` → `Packer`（或 `unique_ptr<Packer>`） |
| `src/pack/pack_service.cpp` | 适配新类型；移除 `make_unique<Packer>()` 间接层 |
| `tests/packer_mock.h` | **删除** |
| `tests/pack_service_mock_tests.cpp` | 改写为真实 Packer + TempDir 集成测试 |
| `src/picture/picture_process.cpp` | 构造调用简化 |
| `src/video/video_process.cpp` | 构造调用简化 |
| `src/app/pipeline.cpp` | 构造调用简化 |
