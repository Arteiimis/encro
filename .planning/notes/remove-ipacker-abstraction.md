---
title: "移除 IPacker 抽象层决策"
date: 2026-04-30
context: "/gsd-explore — 评估 v1.3 引入的 IPacker 抽象基类的必要性"
---

# 移除 IPacker 抽象层决策

## 背景

v1.3 引入 `IPacker` 纯虚抽象接口，作为 `Packer`（生产实现）和 `MockPacker`（测试替身）的共同基类。
`PackService` 通过 `unique_ptr<IPacker>` 持有依赖。

现在评估是否必要。

## 发现

| 维度 | 结论 |
|------|------|
| 生产实现数 | 仅 `Packer`（`final`），无其他实现 |
| 生产消费者数 | 仅 `PackService` 一个类 |
| 生产多态使用 | 零——全部5处构造点均为 `make_unique<Packer>()` |
| MockPacker 使用者 | 仅 `pack_service_mock_tests.cpp`（10个测试） |
| 替代后端规划 | 无（tar/7z 不在任何规划中） |
| 虚拟调度频率 | archive 粒度，非热路径 |
| 签名维护成本 | 3 个方法签名在三处同步维护（ipacker.h / packer.h / packer_mock.h） |
| 可替代测试方案 | `pack_service_tests.cpp` 已用真实 Packer + TempDir 覆盖相同编排路径 |

## 决策: 移除 IPacker

**移除 IPacker 抽象层，Packer 作为唯一实现直接注入 PackService。**

理由：
1. **零生产多态** — 抽象层只服务了 MockPacker 一个测试消费者，为测试而抽象是反模式
2. **无未来替代方案** — 无 tar/7z 或其他后端需求
3. **已有替代测试覆盖** — `pack_service_tests.cpp` 用真实 Packer 覆盖了相同编排逻辑
4. **减少维护税** — 删除 ipacker.h、MockPacker，PackService 直接持有 Packer

MockPacker 的10个测试改写为使用真实 Packer + TempDir 的集成测试，
测试速度略降但覆盖更真实。

## 关联

- Todo: `.planning/todos/pending/remove-ipacker-abstraction.md`
- Requirements: `.planning/REQUIREMENTS.md` (SIMPLIFY-15, SIMPLIFY-16)
