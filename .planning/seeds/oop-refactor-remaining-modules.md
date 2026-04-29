---
title: "其余模块的OOP重构"
trigger_condition: "pack模块PackRequest单一入口重构milestone完成（G-1 ~ G-5 全部达成，测试绿）"
planted_date: 2026-04-30
---

# 其余模块的OOP重构

Pack 模块是第一个完成 OOP 迁移的子系统（v1.3），也是第一个进行接口简化迭代的模块。
Pack 重构中积累的模式（单一入口声明式 API、内部编排隐藏、配置集中注入）可在其余模块
的 OOP 重构中复用。

## 候选模块

- **Picture 模块** (`src/picture/`) — 图片处理管线，当前包含大量自由函数和手写编排
- **Video 模块** (`src/video/`) — 视频处理管线，与 picture 结构类似
- **Core / 配置模块** (`src/core/`, `src/cmd/`) — 配置解析、上下文管理

## Pack 重构中可复用的模式

1. 调用方不应手动编排流程——只需声明需求
2. 内部类型不应泄漏到模块边界之外（`pack::detail::` → 模块内部）
3. CLI 配置统一注入，不硬编码行为参数
4. 保留接口抽象（IPacker → 未来可能是 IPictureProcessor 等）以维持测试能力
