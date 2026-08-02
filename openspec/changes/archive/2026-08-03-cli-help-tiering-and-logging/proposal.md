## Why

`encro -h` dumps all ~30 flags at once, burying the common workflow under advanced options, and several flag descriptions are misleading (`-v` only writes a log file, echo is hidden behind `-e` which depends on `-v`; `--flat` is a dead switch). Logging is fully disabled by default, so ordinary runs leave no diagnostic trace for bug reports.

## What Changes

- **Help tiering**: `-h` shows a brief help with common options only; `-hh` (repeated `-h`) shows all options. Advanced options (`-v`, `--log-json`, `-F`, `--color`, `-I`, `--state-file`, `--force-conflict-handling`, `-x`, `--preset`) are hidden from the brief help. Usage line and `-h` description mention `-hh`; brief help ends with the hint `Run 'encro -hh' to view all options.`
- **Always-on file logging**: every run writes the rotating verbose log file (`%LOCALAPPDATA%/encro/logs/encro_YYYYMMDD_HHMMSS.log`); no flag required. The startup hint `Log file: <path>` is always printed.
- **BREAKING** `-v`/`--verbose` semantics: now echoes logs to the console and disables progress bars (previous `-v -e` behavior). The old `-v` (file-only) is the new default.
- **BREAKING** `-e`/`--verbose-echo` removed; the "requires --verbose" warning branch goes away.
- **BREAKING** `--flat` removed (dead flag — nothing reads it; default flatten behavior unchanged). `--keep` help gains `(default: flatten)`.
- **Help text fixes**: `--resume` described as strict mode ("require matching previous job state; error if missing or mismatched"); `--force-conflict-handling` description rewritten to explain `y|n`; `-q`/`--crf`/`--preset` show defaults as `(=2)` `(=28)` `(=auto)` instead of prose `default=` text; `-j` description drops its redundant prose `default=10` (the `(=10)` display already covers it)
- **Default-value alignment (audit fix)**: the five dead member defaults in `EncodeConfig` (crf 28, nvencPreset p5, outputFormat mp4, videoCodec hevc_nvenc, webpQuality 80) are removed — production always overrides them via designated initializers, so the `value_or` fallbacks in `buildCMD` (28, p5, mp4, hevc_nvenc, 80) become the single source of truth; crf 28 is the intended default, and help text shows `(=28)`, so code, tests, and help agree on 28
- **Job-state mismatch warning**: when a saved state exists but does not match the current config (and no explicit `--resume`), the user is warned instead of the state being silently discarded.

## Capabilities

### New Capabilities

- `cli-help-tiering`: brief vs full help output, which options appear in each tier, `-hh` detection, usage and hint lines
- `cli-flag-cleanup`: `--flat` removal and help-text corrections (`--resume`, `--force-conflict-handling`, `--keep`, default-value display)
- `logging-behavior`: always-on file logging, `-v` echo semantics, console error reporting, `--log-json` companion file behavior

### Modified Capabilities

- `job-state-resume-matching`: add a user-facing warning when a mismatched saved state is discarded during automatic resume

## Impact

- **Code**: `src/cmd/cmd.cpp` (flag defs, formatter, applyMap), `src/cmd/cmd.h`, `src/cmd/config_builder.cpp`, `src/core/app_context.h`, `src/app/prelude.cpp` (setupLogging, hints), `src/app/app_entry.cpp` (failWithHint), `src/logging/setup.{h,cpp}` (LogConfig, sinks, levels), `src/video/video_batch_execution.cpp` (progress-bar gating), `src/core/job_state_store.cpp` + `src/app/pipeline.cpp` (mismatch warning)
- **Tests**: cmd/help-output tests (brief vs full), logging tests (default file creation, echo), job-state tests (mismatch warning), e2e tests referencing `-e`/`-v`
- **No new dependencies.** Breaking changes documented for users of `-e` and `-v`.
