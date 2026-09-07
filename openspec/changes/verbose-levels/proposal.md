## Why

`-v` today jumps straight to a full debug firehose: every log record (millisecond timestamps, module tags, `file:line`, attrs JSON) echoes to stdout in the developer format, interleaved with — and drowning — the product output it was supposed to illuminate. It also inverts expectations in two ways: the clean `error:` line disappears (replaced by a timestamped log record), and there is no way to ask for less output (`--quiet` does not exist). Conventional CLI behavior (git, cargo, npm) is the reverse: diagnostics go to stderr, `-v` adds curated detail, `-vv`/`--debug` unlocks full logs, and errors stay readable at every verbosity.

## What Changes

- The verbose echo moves from stdout to stderr at every level, so `encro <dir> | tee out.txt` captures clean product output while diagnostics stay on the terminal.
- `-v` echoes curated info-level detail in a short format (`info: message` — no timestamp, module tag, `file:line`, or attrs chain); info and warning records echo; error/critical records do not (the clean `error:` console line owns them).
- `-vv` (repeating `-v`) and its alias `--debug` echo the full debug record set in the existing file-log format, on stderr. The file log keeps recording everything at debug regardless of flags.
- Command failures print the clean `error:` line exactly once on stderr in every verbosity mode; the echo stream never replaces it.
- New `--quiet` (long-only; `-q` is taken by image quality) suppresses narration lines and progress bars; errors, warnings, and the run's final summary line still print, and the log file is still written.
- The existing coupling — echo active disables progress bars, with the notice — stays, now stated for both echo levels.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `logging-behavior`: the `-v` echo requirement gains levels (`-v` curated info on stderr, `-vv`/`--debug` full debug), a defined echo format, and an echo filter; the errors-reach-console requirement guarantees the clean error line in every mode instead of letting the echo replace it; a new requirement covers `--quiet`.

## Impact

- `src/cmd/cmd.cpp`: `-v` becomes an occurrence-counted flag, `--debug` added as the level-2 alias, `--quiet` added (General options tier); `src/cmd/cmd.h` parse result carries verbosity + quiet.
- `src/logging/setup.{h,cpp}` and the logging formatters: console echo sink moves to a stderr sink; two echo levels with distinct formatters (short stripping formatter vs the existing file pattern) and level filters; quiet does not touch file logging.
- `src/app/prelude.cpp` and `src/app/app_entry.cpp`: wire flags to logging config; the failure path keeps printing the clean error line under `-v`.
- `src/video/video_batch_execution.cpp` (the existing `verbose` progress-disable gate gains the quiet condition; bars-disabled notice wording), `src/core/progress.cpp` (quiet disables rendering alongside the existing non-TTY gate).
- Ordering: lands after `console-message-conventions` (its kind-dispatched console entry point and clean `error:` line are prerequisites for the quiet gate and the echo filter).
- Tests: verbose-echo assertions, format tests, new quiet-mode and level tests.
