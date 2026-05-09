---
created: 2026-05-09T10:34:09.034Z
title: Enhance CLI option conflict error messages
area: cmd
files:
  - src/cmd/config_builder.cpp:110-112
  - src/cmd/config_builder.cpp:287-289
  - src/cmd/config_builder.cpp:319-321
  - src/cmd/config_builder.cpp:330-332
  - src/cmd/config_builder.cpp:356-374
---

## Problem

当前 `config_builder.cpp` 中的 flag 互斥 / 冲突检测的错误提示比较基础，信息量有限：

- `"--flat and --keep cannot be used together."` — 未提示二选一
- `"--resume and --restart cannot be used together."` — 未说明默认行为
- `"-i/--input and -I/--inputs, not both."` — 未引导正确用法

此外可能缺失部分互斥检测（如 `--pack` + `--pack-only` 同时出现），迁移到 CLI11 后可以利用 CLI11 内置的 `->excludes()` 在 option 定义层直接声明互斥关系，减少 config_builder 中的 ad-hoc 校验。

用户期望:"错误提示更友好，比如不能同时出现的 flag 同时出现时，给出清晰的提示并建议正确用法。"

## Solution

1. **增强现有错误信息** — 每条互斥错误增加引导性建议，例如：
   - `--flat and --keep` → "Cannot use --flat and --keep together. Choose one: --flat (default) flattens output names, --keep preserves subdirectory structure."
2. **利用 CLI11 `->excludes()`** — 在 Phase 19 CLI11 迁移时，对互斥 option pairs 使用 CLI11 内置排除机制，让 CLI11 在 parse 阶段就检测冲突，减少 config_builder 负担
3. **补全缺失的互斥检查** — 审计所有 26 个 option，确认 `--pack` / `--pack-only`、`--verbose` / `--verbose-echo` 等是否存在未覆盖的冲突场景
4. **统一样式** — 所有错误信息统一使用 `eh::makeError()` 返回，保持与现有 config_builder 错误风格一致（小写开头，`--` 前缀 flag 名）
