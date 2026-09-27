## Why

A copy the `organize` run cannot perform is recorded and then dropped.
`executeOrganize` pushes one `copy failed: <source> -> <destination>` line per
failed copy into `ExecuteStats::errors` (`src/organize/execute.cpp:99-101`),
but its only caller builds the `ReportData` aggregate without that field
(`src/organize/pipeline.cpp:371-379`): `ReportData::copyErrors` stays empty, the
renderer's loop over it (`src/organize/report.cpp:79`) iterates nothing, and
`ExecuteStats::errors` reaches no production reader (only two `stats.errors.empty()`
assertions on `executeOrganize`'s direct return in
`tests/organize/stage_tests.cpp:209,244`). A run whose copies failed therefore
ends on `scanned M images: copied N` with the difference unnamed and nothing in
its exit code to notice — the user cannot tell which images are missing from
`organized/`, or why. The field's own declaration calls it "per-file copy
failures; visible in report" (`src/organize/report.h:25`), so the renderer and
the field were written to surface it and only the wiring is absent.

## What Changes

- `runOrganize` passes `stats.errors` through as `ReportData::copyErrors`, so
  the report lists each failed copy (source and destination) after the totals
  line, in the report's existing two-space indented detail-line style.
- The run's exit code is unchanged: a run with failed copies still ends with the
  code it ends with today (0 for a completed run). A failed copy is not a run
  failure — nothing is corrupted, the originals are untouched, and re-running
  retries exactly the copies that are missing — and the report is what names the
  incomplete output tree. This is a recorded decision (design.md D2), made
  explicit in the spec so a later exit-code change is a deliberate edit.
- No report format change beyond the lines the renderer already knows how to
  print: both the failure text and its indentation exist today.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `image-character-organize`: the report's contract gains a requirement that a
  failed copy is named in the report (source and destination) and does not
  change the run's exit code.

## Impact

- `src/organize/pipeline.cpp` — the `ReportData` aggregate gains
  `.copyErrors = stats.errors`; no other code moves.
- `tests/organize/pipeline_tests.cpp` — one `[organize]` case: a run whose
  destination tree cannot be written prints the failed copy in its report and
  exits 0.
- No CLI surface, config, cache or job-state change. The defect recorded in
  `docs/backlog.md` ("organize copy failures are collected and then dropped") is
  resolved by this change; that file is not edited here.
