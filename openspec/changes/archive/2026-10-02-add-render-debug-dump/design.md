# Design — add-render-debug-dump

## Context

All console output funnels through two layers: every message through `terminal::write(Stream, text, newline)` (`src/infra/terminal.cpp:293`, the single exit point behind `terminal::print/println/format`), and progress bars through `ProgressContext` → indicators. Two constraints shape the design:

- indicators 2.3's `DynamicProgress::print_progress` writes cursor escapes and newlines straight to `std::cout`, bypassing the per-bar `option::Stream` — this is why `ProgressContext::render()` is gated off entirely for non-TTY stdout (`progressBarsAllowed()`, `src/core/progress.cpp:22`). Redirecting real bar frames to a dump would leak those escapes into piped stdout, so real frames are unobtainable.
- `ProgressContext` already stores every bar's final state in-house: one label slot per bar (`postfixes_`, seeded with the `addBar` prompt and overwritten by `setPostfixText` — exposed as `postfixText()`), the progress value, and the indeterminate flag (all exposed as read-only accessors for tests).

## Goals / Non-Goals

**Goals:**

- One hook in the message layer plus one frame append in the bar layer; nothing else observes the env var.
- Dump records are copies of rendered bytes — zero change to rendering, colors, quiet, or bar lifecycle behavior.

**Non-Goals:**

- Dumping the verbose-echo stream (`-v`/`-vv`): that is a logging sink writing to stderr through spdlog, not through the console layer; it has its own format and spec (`logging-behavior`).
- Golden-file render tests, frame-by-frame bar animation, or ANSI-stripped views — the dump carries raw bytes; a plain-text view is obtained by running with colors off.
- Cross-process dump file locking (debug-only, single user).

## Decisions

### D1: Hook inside `terminal::write`, read the env on every write

The write hook lives at the top of `terminal::write` — after the quiet gate has already returned in `terminal::print`, so suppression order is inherited for free, and before `fmt::print`, so the dump sees exactly the bytes headed to the console. The env var is read with the existing `processenv::readNonEmptyEnvVar` per write rather than cached at startup: one CRT env lookup is cheap for message-volume output, and per-write reading keeps the hook controllable from in-process tests (`ScopedEnvVar` in `tests/test_utils.h:238+`) regardless of Catch2's random case order.

Alternative rejected — startup-time caching into an atomic path: a static-initialized cache freezes before any test can set the variable and couples the hook to process init order.

### D2: Record = stream marker + raw bytes, appended in binary mode

Each record is `[out] ` / `[err] ` followed by the write's text plus its newline byte if the call had one. The file is opened per write with `std::ofstream(path, app | binary)` — binary mode is load-bearing on Windows, where text mode would rewrite `\n` into `\r\n` and corrupt the "exact bytes" contract. Per-write open/close also sidesteps handle lifetime entirely; message volume makes the cost irrelevant. A single static mutex serializes concurrent appends so interleaved records from parallel workers stay whole (the console itself has no such guarantee today; the dump should not be worse).

Alternatives rejected — two files per stream (loses interleaving, the third backlog symptom); an ANSI-stripped record (loses color-structure inspection, the first two symptoms; both views are reachable by toggling `--color`).

### D3: Bar frames are state-built plain text, appended in `eraseBars` before the TTY gate

`eraseBars` currently sets `cleared_ = true` and then returns early when `!progressBarsAllowed()`. The frame append runs right after `cleared_ = true` and before that gate, so it fires identically on TTY and non-TTY runs. Each bar becomes one line built from stored state: the stored label slot's current text (`postfixText()` — the `addBar` prompt until `setPostfixText` overwrites it, i.e. the bar's current label, so the frame needs no second store), a rendered fill-and-percentage core for determinate bars or an indeterminate marker, and the progress value from `progressValue()`. The label text is dumped raw — the width-fitted copy `applyBarText` pushes into the bar option is a per-render view, not state; the frame's job is the complete final state, and a possibly over-wide line is more informative than a truncated one. The line is plain text with no styling sequences — the spec's frame is a final-state record, not a reproduction of indicators' live drawing.

Alternatives rejected — force `render()` on non-TTY when dumping (leaks cursor escapes to piped stdout via the hardcoded `std::cout` writes); a full second renderer of indicators' layout (drift risk for zero inspection value — the frame's job is "what state did each bar end in").

### D4: Tests reach the hooks through the public surfaces only

Unit cases set the env var via `ScopedEnvVar`, drive `terminal::print`/`terminal::println` (message layer) and a `ProgressContext` with two bars plus `eraseBars` (frame layer), and assert on the dump file's contents: record order and markers, escape-sequence presence under `configure(ColorMode::Always)` vs absence under `ColorMode::Never` (with `terminal::reset()` restoring state), and frame lines for determinate and indeterminate bars. The spec's TTY frame scenario is covered structurally rather than by a case: the append writes only the dump file and touches no console stream, and the test process is not a TTY so the non-TTY half is asserted directly (dump frames present, console untouched) while the existing suites staying green pins that unset-variable behavior is unchanged. No e2e: both hooks are observable from within the process.

## Risks / Trade-offs

- [Threaded writers interleave mid-record] → static mutex around the append (D2); the dump is strictly more coherent than the console.
- [Two processes dumping to one file corrupt records] → accepted: debug-only, single user, documented with a `ponytail:` comment at the append site; upgrade path is file locking if it ever bites.
- [Frame drift: stored state diverges from what the TTY actually drew] → bounded: the frame draws from the same state `render()` would, and the frame's contract (final state, not animation) makes width/scroll details out of scope by spec.
- [Env var set in a user's shell poisons every run's perf] → per-write cost is one env lookup plus one file open only when the var is non-empty; unset/empty short-circuits before any I/O.
