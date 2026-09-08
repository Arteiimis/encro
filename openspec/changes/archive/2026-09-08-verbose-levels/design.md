## Context

`src/logging/setup.cpp` builds the console echo sink (`-v` → `stdout_color_sink_mt`) with every logger at debug level and the file pattern; `src/app/prelude.cpp` maps `cmd.verbose` to echo, and `video_batch_execution.cpp` prints the bars-disabled notice. The failure path (`app_entry.cpp`) today replaces the clean error line with the echoed log record under `-v`. See proposal.md - Why.

## Goals / Non-Goals

**Goals:**

- `encro ... | tee` gets clean product output; diagnostics (echo) stay on stderr.
- `-v` readable by a human mid-run; `-vv`/`--debug` preserves today's full diagnostic surface unchanged.
- Errors equally readable at every verbosity.

**Non-Goals:**

- No change to the file log (pattern, rotation, level, RUN SUMMARY record).
- No change to `--log-json`.
- No per-logger or per-module filtering (`--verbose=video` style) — no demonstrated need.

## Decisions

- **D1 — occurrence counting for `-v`; `--debug` is the level-2 alias.** CLI11 counts repeated flags (`count()`), so `-vv` falls out of the existing single `-v` registration (verified: short-flag remainder re-push in CLI11's parser); `--debug` is registered as a second flag that sets the level to 2. Level values: 0 (off), 1 (curated), 2 (full), clamped — `-vvv` and beyond mean level 2, and `--debug` combined with `-v` still means level 2. Alternative rejected: separate `--debug` boolean with independent semantics — two overlapping knobs to document and test.
- **D2 — echo sink is stderr.** Replace the stdout sink with `stderr_color_sink_mt` (plain variant when colors are off — the existing color-mode plumbing decides). Product output never mixes with echo under redirection.
- **D3 — two echo formatters; the short one strips what the macros bake in.** The `LOG_*` macros embed `[file:line]` at the head of the message body and append `[attrs: …]` / `[context: …]` chains at the tail, so a bare pattern cannot produce the short format. Level 1 therefore uses a small custom formatter that strips the leading location and the trailing attribute/context chains from `%v` and renders `level: message` — reusing the same message-tail parsing the JSON formatter already does for its fields; level 2 reuses the exact file pattern. Both live in `src/logging` next to the existing formatters. Alternative rejected: echo-path logging macros (`LOG_ECHO_*`) — every call site gains a second variant forever for one sink's benefit.
- **D4 — level-1 echo filter: info + warning.** Error/critical records are not echoed at `-v` because every command failure already renders one clean `error:` line (guaranteed by the `console-message-conventions` change); echoing would duplicate it. `-vv` echoes everything, where duplication is the point (full diagnostic surface). The failure path in `app_entry.cpp` loses its verbose special case and always prints the clean line.
- **D5 — `--quiet` is a narration gate, not a log level.** A process-wide quiet flag checked where narration is emitted (the kind-dispatched terminal entry point from the `console-message-conventions` change — this change lands after it) plus the existing progress-disable path; the final summary line, errors, warnings, and the log hint bypass the gate, and failure-path output (the failed-file list) is not narration, so it prints under quiet too. Quiet and echo are independent (`--quiet -v` suppresses narration while the echo stream still emits; the bars-disabled notice is suppressed under quiet because bars are already off). Long-only flag (`-q` is image quality). Placed in the visible General options tier; `--debug` stays in the hidden tier. Alternative rejected: mapping quiet onto spdlog level — narration lines are not log records and would stay visible.
- **D6 — bars-disabled notice wording covers both levels.** The notice text stays one line ("echo enabled: progress bars disabled") shared by `-v` and `-vv`.

## Risks / Trade-offs

- [Existing `-v` users lose stdout echo and full debug at one `-v`] → Deliberate: `-vv`/`--debug` restores the old surface exactly; the release note names the mapping.
- [Echo and clean error line can both appear at `-vv`] → Accepted duplication at the firehose level; never at `-v`.
- [Quiet interacts with prompts] → Prompts are interactive and unaffected; scripted callers already use `-y` (or get the EOF-abort behavior unchanged).

## Migration Plan

Single build, no persisted state. Ordering: lands after `console-message-conventions`, whose kind-dispatched entry point and clean `error:` line this change's quiet gate and echo filter build on. Rollback is reverting the commit.
