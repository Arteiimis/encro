## Why

The bracketed message badges (`[error]`, `[warn]`, `[done]`, `[info]`, `[hint]`, `[?]`) are carried by color: in colored output they double-mark the same fact (`[error] Error: ...`), and in `NO_COLOR`/piped output they disappear entirely, taking the severity information with them. At the same time the two console streams are used inconsistently — the main pipeline prints error text on stdout while the log-file hint goes to stderr, and the `config`/`completion` subcommands do the opposite — so redirecting or capturing output produces mixed, out-of-order records. Adopting the git/cargo-style plain-text severity prefix fixes both: the prefix is always printed regardless of color, and a single stream rule decides where each message goes.

## What Changes

- Remove all bracketed badges from console output. Success and informational lines print bare text (the message already states what happened); headings keep their text with color-only styling.
- Severity is carried by always-printed lowercase text prefixes: `error:`, `warning:`, `hint:`. Color (red/yellow/gray) decorates the prefix when colors are enabled and is never the sole carrier of severity.
- One stream rule for the whole CLI: errors, warnings, and hints go to stderr; narration, progress, results, and summaries (including their failed-file and attention lists) go to stdout. This moves the top-level failure line (`Error: Invalid arguments: ...` → `error: Invalid arguments: ...` on stderr, message bodies keep their sentence case) and the in-pipeline error lines to stderr, while the log-file hint stays where it is. Standalone cancellation notices go to stderr.
- The `config` and `completion` subcommand diagnostics (already lowercase on stderr) adopt the same `error:` / `warning:` prefixes.
- Interactive prompts drop the `[?]` badge; the question text itself ends with `(Y/n):`, unchanged otherwise (Enter still means yes, `-y` still skips).
- No change to color detection (`--color auto|always|never`, `NO_COLOR`, TTY probing) or to the inline `value`/`path`/`count` token coloring.
- The verbose echo stream keeps its own level tags (owned by `logging-behavior`); this change governs the product console channel only.
- Ordering: lands first among the sibling output changes (`pipeline-output-declutter`, `verbose-levels`, `failure-reason-visibility`), which build on its kind-dispatched entry point.
- Output-format change for anything that parses encro's console text or relies on errors appearing on stdout (none known in-repo; tests are updated with the change).

## Capabilities

### New Capabilities

- `console-output-conventions`: severity prefixes and their always-printed (color-independent) rendering, the stdout/stderr stream discipline for every message kind, and prompt rendering.

### Modified Capabilities

(none — no existing spec pins badge formats or error streams; `logging-behavior`'s hint-on-stderr already matches the new rule, and `plan-output-formatting` already prohibits per-line badges in its tables.)

## Impact

- `src/infra/terminal.{h,cpp}`: badge rendering becomes prefix rendering (`error:` / `warning:` / `hint:` / none); message kinds gain a stream (stdout vs stderr) so `print`/`eprint` selection follows the kind; the redundant `Error:` literal in top-level failure messages is dropped in favor of the prefix.
- `src/app/app_entry.cpp` (`failWithHint` and wrappers, including the help hint), `src/cmd/config_command.cpp`, `src/cmd/completion_install.cpp`, `src/app/pipeline.cpp`, `src/logging/setup.cpp` (startup warnings), and the in-pipeline `terminal::println(Error/Warning/...)` call sites across `src/video`, `src/picture`, `src/preview`, and `src/organize` (organize's diagnostics included; also dropping the duplicated `Error:` literal at the scan-failure and probe-failure sites): call sites pass the kind, the stream follows from it.
- Tests asserting console text (`tests/**` catching `[error]`/`[warn]`/`[done]` strings or stream placement) are updated in the same change; e2e assertions that capture stdout/stderr separately gain the new split.
