# Phase 20: CLI Color Deepening - Context

**Gathered:** 2026-05-09
**Status:** Ready for planning

<domain>
## Phase Boundary

Deliver semantic terminal coloring across all CLI output: `--help` (section-based coloring via formatter_fn), `--version` (new flag, colored), and error messages (verified already unified). Full NO_COLOR standard compliance via `colorsEnabled()` gating. Zero user-visible behavioral change beyond color — all parsing, validation, and workflow paths identical to Phase 19.

Requirements: COLR-01 through COLR-05.
</domain>

<decisions>
## Implementation Decisions

### --help 着色方案 (COLR-01)
- **D-01:** 三层色彩体系：
  - Usage / 行首描述行 → `steel_blue` + bold (`MessageKind::Usage`)
  - 选项组标题（General, IO, Processing, FileOp）→ `steel_blue` + bold (`MessageKind::OptionGroup`)
  - 选项名（--help, -i, --input, etc.）→ `light_cyan` (`MessageKind::OptionName`)
  - 选项描述文字 → 普通文本无色 (`MessageKind::OptionDesc`)
  - 默认值标注 `(=mp4)` → 跟随 OptionDesc 无色，不单独着色
- **D-02:** formatter_fn 着色注入顺序：先计算纯文本列宽 & 排版填充（padding），然后 `terminal::styledText(Stream::Stdout, kind, text)` 包裹每个元素，最后拼接返回 string。**ANSI padding before color injection** — 着色前 padding 避免转义码破坏列对齐。

### --version 设计 (COLR-04)
- **D-03:** 新增 `--version` flag，位置在 General 选项组 `--help` 之后。
- **D-04:** 输出格式：`encro v1.6 (build: YYYY-MM-DD HH:MM:SS)` — 版本号与 milestone v1.6 对齐（非 build version 0.1.5）。
- **D-05:** 着色通过 `terminal::println(Version, ...)` — 新增 `MessageKind::Version`，编译时间戳复用现有 `compileTimestamp()` 逻辑。

### 错误消息统一 (COLR-03)
- **D-06:** 已确认所有错误路径已通过 `failWithHint()` → `terminal::println(Error, ...)` 统一着色。解析错误（CLI11 native）、配置校验、toolchain、pipelines — 全部一致，无需额外工作。

### MessageKind 扩展 (COLR-02)
- **D-07:** `MessageKind` enum 新增 5 个值，追加在末尾保持向后兼容：`Usage`, `OptionGroup`, `OptionName`, `OptionDesc`, `Version`。
- **D-08:** 每个新值需要 `styleFor()` 映射（steel_blue, light_cyan 等）和 `defaultBadgeLabel()` case 处理。`Version` 不使用 badge prefix。

### NO_COLOR 合规 (COLR-05)
- **D-09:** 所有新着色路径通过 `terminal::styledText()` 传入 `Stream::Stdout` — 内部已调用 `colorsEnabled(stream)` 门控。Piped 输出自动降级为纯文本。
- **D-10:** 无需额外实现 `NO_COLOR` 环境变量检测 — `terminal::configureFromColorString()` 已在 prelude 中处理 `--color auto|always|never`。

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements & Roadmap
- `.planning/REQUIREMENTS.md` §CLI着色深化 — COLR-01 through COLR-05 (5 requirements for Phase 20)
- `.planning/ROADMAP.md` §Phase 20 — Success criteria: colored --help/--version/errors, NO_COLOR compliance, 5 new MessageKind values

### Architecture & Stack
- `.planning/codebase/STACK.md` — Dependency overview (fmt color, terminal module)
- `.planning/codebase/ARCHITECTURE.md` — CLI→Config→Pipeline flow, terminal module at Infrastructure layer
- `.planning/codebase/CONVENTIONS.md` §Enums — PascalCase enum values, additive-only extension pattern

### Prior Phase Context (decisions locked)
- `.planning/phases/19-cli11-migration/19-CONTEXT.md` — formatter_fn structure (formatOptionHelp/formatGroupHeader/formatOptionName), D-04 (no color → Phase 20), D-03 (helper-based formatter_fn)
- `.planning/STATE.md` — Pitfall #7 (formatter_fn zero test coverage, need smoke tests), Pitfall #2 (colorsEnabled() per-stream gating)

### Source Files (MUST READ during planning)
- `src/infra/terminal.h` — MessageKind enum, styleFor(), styledText(), colorsEnabled(), println() API
- `src/infra/terminal.cpp` — styleFor() mapping table, defaultBadgeLabel(), configureFromColorString()
- `src/cmd/cmd.cpp` — formatter_fn lambda + helpers (formatOptionHelp, formatGroupHeader, formatOptionName, makeHelpFormatter) — the color injection target
- `src/cmd/cmd.h` — CmdParseResult struct (version field to add)
- `src/app/app_entry.cpp` — printHelp(), helpIntroLine() (intro line now rendered by formatter_fn per quick task 260509-tjc)
</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `terminal::styleFor(MessageKind)` — maps enum → `fmt::text_style`. Extend with 5 new entries.
- `terminal::styledText(Stream, MessageKind, text)` — returns colored string (not writes). Use in formatter_fn.
- `terminal::println(MessageKind, fmt, args...)` — writes colored line. Use for --version output.
- `terminal::colorsEnabled(Stream)` — already gates all color paths. Zero changes needed.
- `terminal::defaultBadgeLabel(MessageKind)` — returns badge prefix strings. Extend for new kinds.
- `formatOptionHelp()`, `formatGroupHeader()`, `formatOptionName()` in `src/cmd/cmd.cpp` — color injection points.
- `compileTimestamp()` in `src/app/app_entry.cpp` — build timestamp formatting for --version.

### Established Patterns
- MessageKind enum: PascalCase values, added at end, no renumbering (additive-only per Phase 19 research).
- `styleFor()`: switch-case mapping each MessageKind to fmt::text_style, currently 8 entries.
- `defaultBadgeLabel()`: switch-case returning std::string_view badge labels (empty for non-badge kinds).
- formatter_fn: pure string assembly — receives `CLI::App const*`, returns `std::string`.

### Integration Points
- `src/cmd/cmd.cpp:makeHelpFormatter()` — the lambda body (~40 lines). Insert `styledText()` calls around each rendered element.
- `src/cmd/cmd.cpp:formatOptionHelp()` — modify to accept MessageKind enums for option name and description coloring.
- `src/cmd/cmd.cpp:formatGroupHeader()` — wrap group title with `styledText(Stdout, OptionGroup, ...)`.
- `src/cmd/cmd.cpp:commandLineInit()` — add `--version` flag registration, compute introLine with milestone version.
- `src/app/app_entry.cpp:handleParseAndHelp()` — add `cmd.version` check, output via `terminal::println(Version, ...)`.
- `src/cmd/cmd.h` — add `bool version` field to `CmdParseResult`.
</code_context>

<specifics>
## Specific Ideas

- Phase 19 research established ANSI padding must happen before color injection — compute max plain-text name width first, pad to that width, THEN apply `terminal::styledText()`.
- formatter_fn lambda already captures group `App*` pointers — no structural change needed for color injection.
- `--version` should exit 0 immediately after printing, same pattern as `--help`.
- Progress bar coloring is out of scope (indicators library has own color system — deferred to v2+ per STATE.md).
</specifics>

<deferred>
## Deferred Ideas

- Progress bar coloring — indicators library has own color system, separate evaluation needed. Deferred to v2+.

### Reviewed Todos (not folded)
- `2026-05-09-enhance-cli-option-conflict-error-messages.md` — CLI option conflict error messages (score 0.6, area: cmd). Reviewed but not folded: Phase 20 focuses on coloring existing error paths, not enhancing error message content. Belongs in its own quick task or future phase.
</deferred>

---

*Phase: 20-CLI Color Deepening*
*Context gathered: 2026-05-09*
