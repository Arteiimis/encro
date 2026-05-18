---
title: --dry-run 设计决策
date: 2026-05-19
context: 探索阶段 — /gsd:explore 讨论产出
---

## 核心定位

`--dry-run` = 正常跑的全部前置检查 + 预览即将做什么 + 不真正执行（不编码、不压缩、不打包、不写 job state）。

## 输出结构：三层递进

```
 Dry-run 概要（命令行复现）

── Validation (环境 & 配置) ──
  ffmpeg/ffprobe 路径 & 版本
  input path 存在 & 可读
  output parent 可写
  参数组合无冲突

── Scan (文件扫描 & job state) ──
  文件数量 & 总大小
  --resume 时展示已完成/待处理计数

── Plan (执行预览) ──
  编码文件数 → 目标格式
  预估时间 & worker 数
  打包 archive 数 & 大小
  job state 文件操作
```

每一层如果失败，后续层跳过不展示。

## 检查边界决策

- **Job state file — 只读不写。** `--resume --dry-run` 读取已有 job state 展示恢复信息；不存在时报告"无可恢复状态"。绝不创建或修改 job state 文件。
- **输出目录 — 只检查父目录可写，不创建任何东西。** 正常运行时目录不存在会自动创建，dry-run 不重复此逻辑。仅检查父目录存在且可写。
- **文件扫描 — 完整执行。** 递归扫描真实进行以获取准确计数，不是估算。

## 与正常运行的校验层级区分

正常运行时就应该做的检查（ffmpeg 缺失、路径不存在、参数冲突）dry-run 也做——不重复实现，只是 dry-run 在检查完毕后退出而不进入执行阶段。

正常运行时不会提前检查的（输出可写性、文件规模、ZIP 数量预估）是 dry-run 独有的价值。

## 输出样式

dry-run 自身就是诊断命令，默认输出全貌，不需要 `--verbose` 叠加。输出使用语义着色（OK/WARN/FAIL），复用 MessageKind 体系。
