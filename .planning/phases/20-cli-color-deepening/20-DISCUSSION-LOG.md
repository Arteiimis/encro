# Phase 20: CLI Color Deepening - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-09
**Phase:** 20-cli-color-deepening
**Areas discussed:** --help 着色方案, --version 设计, 错误消息统一, formatter_fn 着色注入方式

---

## --help 着色方案

| Option | Description | Selected |
|--------|-------------|----------|
| 推荐方案 | steel_blue heading + light_cyan options + plain descriptions | ✓ |
| 保守方案 | 仅选项名着色，标题和描述保持无色 | |
| 自定义 | 用户描述需要的效果 | |

**User's choice:** 推荐方案 ✓
**Notes:** 三层色彩体系：Usage/Heading → steel_blue bold, OptionGroup header → steel_blue bold, OptionName → light_cyan, OptionDesc → plain text. 默认值标注跟随 OptionDesc 不单独着色.

---

## --version 设计

| Option | Description | Selected |
|--------|-------------|----------|
| v1.6 + build | encro v1.6 (build: YYYY-MM-DD HH:MM:SS) | ✓ |
| 仅 v1.6 | 只显示版本号，无构建时间戳 | |

**User's choice:** v1.6 + build — 版本号与 milestone v1.6 对齐
**Notes:** --version 在 General 组 --help 之后，通过 terminal::println(Version, ...) 着色，编译时间戳复用 compileTimestamp().

---

## 错误消息统一

| Option | Description | Selected |
|--------|-------------|----------|
| 保持现状 | 所有错误路径已通过 failWithHint() → terminal::println(Error) 统一 | ✓ |

**User's choice:** 保持现状 ✓
**Notes:** 解析错误（CLI11 native）、配置校验、toolchain、pipeline — 已全部走 Error 着色路径，无需额外工作.

---

## formatter_fn 着色注入方式

| Option | Description | Selected |
|--------|-------------|----------|
| 推荐方案 | 排版→styledText→拼接→返回 string（不直接 println） | ✓ |

**User's choice:** 推荐方案 ✓
**Notes:** 先计算纯文本列宽做 padding 对齐，然后 terminal::styledText(Stream::Stdout, kind, text) 包裹着色，最后拼接返回。符合 ANSI padding before color injection 要求.

---

## the agent's Discretion

无 — 所有领域均由用户确认.

## Deferred Ideas

- Progress bar coloring → v2+ (indicators library has own color system)
- CLI option conflict error message enhancement → own quick task (not folded into Phase 20)
