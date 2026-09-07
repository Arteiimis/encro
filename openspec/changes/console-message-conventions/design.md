## Context

`src/infra/terminal.{h,cpp}` renders every console message from a `MessageKind` (Error/Warning/Success/Info/Hint/Prompt/Heading) into a colored bracketed badge, and offers two print families (`print`/`println` → stdout, `eprint`/`eprintln` → stderr) that each call site picks by hand. Today that produces the problems in the proposal: badge-as-color (severity lost without color), doubled markers (`[error] Error: ...`), and hand-picked streams that disagree between the main pipeline (errors on stdout) and the subcommands (errors on stderr). See proposal.md - Why.

## Goals / Non-Goals

**Goals:**

- One place decides prefix text and stream per message kind; call sites keep passing only a kind.
- Severity readable in plain text under every color mode, including `--color always` piped into a log.
- stdout of a run equals its product output (narration, progress, results).

**Non-Goals:**

- No change to color detection (`--color`, `NO_COLOR`, TTY probing) or inline `value`/`path`/`count` token styling.
- No change to the verbose log echo sink — the `verbose-levels` change owns the echo stream and levels.
- No message-text rewrite beyond removing the duplicated `Error:` literals (top-level failure path, scan-failure, and probe-failure sites); wording polish belongs to the output-declutter change.

## Decisions

- **D1 — kind-owned rendering and stream.** `terminal` maps each kind to (prefix, color, stream): Error → `error:`/red/stderr, Warning → `warning:`/yellow/stderr, Hint → `hint:`/gray/stderr, Success/Info/Heading → no prefix/green/blue/steel/stdout, Prompt → no prefix/cyan/stdout. Run-summary blocks (count line, failed-file lists, attention entries, preview hints) print on stdout as one unit even though their individual lines read as warnings; standalone cancellation notices go to stderr. A single `message(kind, ...)` entry point dispatches; the `print`/`eprint` families remain for stream-explicit cases (help text, script emission). Alternative rejected: converting every call site to `eprint*` — ~25 severity sites, each one a chance to pick the wrong stream again.
- **D2 — prefixes are plain text, colors decorate.** The prefix literal is always in the output string; ANSI codes wrap it when enabled. This is what makes `--color never` and piped output keep severity, and it replaces the badge (no bracket text remains).
- **D3 — `warning:` spelling, message bodies keep their sentence case.** `warning:` matches gcc/cargo (the spdlog level name stays `warn` — different channel). Message bodies are not re-cased; the body edits are dropping the redundant `Error:` literal that `failWithHint` prepends and the same literal at the in-pipeline scan-failure and probe-failure sites, so failures render `error: Invalid arguments: ...`. Alternative rejected: lowercasing all bodies — churn across every message for no functional gain; the declutter change touches wording where it matters.
- **D4 — prompts stay on stdout.** Prompts are interactive elements colocated with the progress display (stdout); moving them to stderr would desync prompt and bars on terminals where the streams render at different speeds. The badge is dropped; the `(Y/n): ` tail is the affordance.
- **D5 — subcommand conformance via the same entry point.** `config_command.cpp` / `completion_install.cpp` diagnostics already go through kind-dispatched stderr calls; the change is badge→prefix rendering plus moving their hint/success lines onto the right stream, not bare-text→kind-dispatched. Their existing lowercase style already matches the target register.

## Risks / Trade-offs

- [Tests asserting badge literals or stdout placement break broadly] → Expected and accepted: the sweep is mechanical (`[error] ` → `error: `, stream swaps in e2e captures) and lands in the same commit; `xmake test-parallel` gates it.
- [Warnings moving to stderr interleave with stdout progress bars when both go to a terminal] → Same visual result as today (both paint the same screen); when streams are redirected separately the split is the desired behavior.
- [`2>&1` capture order between the error line and the hint line is not guaranteed] → Inherent to multi-stream CLIs; both lines are self-describing so order does not matter.

## Migration Plan

Single build, no persisted state. Any external script parsing the old badge format must switch to the `error:`/`warning:` prefixes or to exit codes — none exist in-repo. Rollback is reverting the commit.
