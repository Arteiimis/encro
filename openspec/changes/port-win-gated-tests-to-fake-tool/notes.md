# Implementation notes: port-win-gated-tests-to-fake-tool

Per-suite mapping from batch-script behaviors to fake-tool knobs, plus any
relaxed assertions (per design D3 default stance).

## New fake-tool knobs (added this change)

| Knob | Semantics |
| --- | --- |
| `ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE=<path>` | Enables per-call schedules; child bumps a persistent counter per invocation |
| `ENCRO_FAKE_FFMPEG_CALL_PLAN="<n>[-]:<delayMs>:<exit>[;...]"` | `2:` targets only call 2; `2-` targets call 2 and every later call; delayed-yet-successful calls express cancel-mid-batch, delayed exit-130 expresses stop-during-encode |
| `ENCRO_FAKE_FFMPEG_FAIL_OUTPUT_BYTES=<n>` | Writes an n-byte output before exiting non-zero (partial-file cleanup flows) |
| `ENCRO_FAKE_FFMPEG_PROGRESS_NO_END_TIME=1` | Suppresses the `out_time_us=` progress line (segment-end fallback warning path) |
| `ENCRO_FAKE_FFMPEG_INPUT_LOG=<path>` | Successful invocations append their `-i` input path (replaces survivor-marker scripts) |

Progress-file emission now also creates missing parent directories.

Unit contract for all knobs lives in `[fake-tool]` cases appended to
`tests/video/encode_probe_tests.cpp` (written test-first).

## picture_compress_tests.cpp (9 gated cases) - ported

Batch scripts mapped to: default success / `EXIT_CODE=1` /
`EXIT_CODE=1 + FAIL_OUTPUT_BYTES=64`. Mixed good/fail batch case uses one tool
copy with a scoped failure env around phase B. Kept intentionally platform-
branched (tests src behavior, not tool faking): the custom ffmpeg path
assertion in buildCMD (Windows-unquoted vs POSIX-quoted command format).

## picture_process_tests.cpp (10 gated cases) - ported

Success scripts ("fake-compressed-jpeg", multi-line bodies) → plain success;
content never asserted, only existence/extensions, so `OUTPUT_BYTES` controls
size comparisons directly (`16` beats 32-byte sources; `512` loses to an
8-byte source for the keep-larger-source fallback). `%*` echo script replaced
by `ENCRO_FAKE_TOOL_LOG_FILE` with tab-separated assertion (`-q:v\t2`).
Counter scripts (`counterPath`) → `CALL_PLAN`: first-call-ok = `2-:0:1`,
second-call-slow-cancel = `2-:3000:0`.

## app/pipeline_picture_tests.cpp (10 gated cases) - ported

Empty-output scripts replicated exactly via `OUTPUT_BYTES=0`.
Second-call-slow(+marker, exit 130 after delay) → `CALL_PLAN "2-:7000:130"`
plus `INPUT_LOG` replacing the survivor-marker file (A7 reads line 1).
Counting script → a fresh `CALL_COUNT_FILE` per phase; invocation-count
assertions read that counter directly.
**Multi-phase gotcha documented by the code**: phase 1's plan env persists
into phase 2, so each resume/restart ctx sets
`CALL_PLAN=""` (noPlan) explicitly.

## preview_process_tests.cpp / video/encode_probe_tests.cpp - de-gated

Private `ScopedEnvVar` / `copyFakeTool` deleted in favor of shared helpers
(`test_utils.h`). encode_probe's duplicate was already portable (setenv
branches) - pure consolidation.

## video/video_process_orchestration_tests.cpp (8 gated cases) - ported

Standard progress-emitting script → default knobs; its "progress without
out_time_us" shape is now expressed by `PROGRESS_NO_END_TIME=1`
(segment-end fallback case). ffprobe JSON echo scripts → fixture file +
`ENCRO_FAKE_FFPROBE_JSON_FILE`. Retry-tier case (`fsutil createnew` big
output then failures) → `OUTPUT_BYTES=22020096` + `CALL_PLAN "2-:0:1"`.

## video_info_tests.cpp (not counted in the original 45)

Two webp-prewarm cases configured fake ffprobe under `_WIN32` only; now both
platforms use role-copied fake ffprobe + JSON fixture, making prewarm
assertions platform-independent.

## Remaining `_WIN32` references in tests/ (accepted, out of scope)

All remaining references either branch on platform *behavior being tested*
(crash handling, console width, COLUMNS, stop-signal semantics, logging file
locking, utils' Win32 helpers, filtergraph font constant) or live inside the
fake tool / test harness implementations themselves. Zero test cases are
excluded from compilation because they impersonate an external media tool.
