## Context

See `proposal.md` — Why. Every piece of the failure path exists except one
link; this design pins what actually holds today, since the diagnosis recorded
in `docs/backlog.md` is stale in one respect (it says the error vector is merely
unwired from the report; in fact it has no reader at all):

- `executeOrganize` (`src/organize/execute.cpp:99-101`) fills
  `ExecuteStats::errors` with `copy failed: <source> -> <destination>` for every
  copy `copySafely` refuses (the staging copy fails, or the final rename does).
- `ExecuteStats::errors` is declared "per-file copy failures; never fatal"
  (`src/organize/execute.h:23`) and has exactly one writer and no production
  reader: two assertions on the direct return
  (`tests/organize/stage_tests.cpp:209,244`) are its only readers, and it is
  dropped at its single call site, so no test can observe the loss through the
  public path.
- `ReportData::copyErrors` is declared "per-file copy failures; visible in
  report" (`src/organize/report.h:25`) and `renderReport`
  (`src/organize/report.cpp:79`) appends one `  <error>` line per entry; both
  are dead until something fills the field.
- `runOrganize`'s `ReportData` aggregate (`src/organize/pipeline.cpp:373-379`)
  assigns `folders`, `scanned`, `copied`, `skippedExisting` and `cacheHits`,
  and never `.copyErrors`; the command prints `renderReport(*runResult)` and
  returns 0 (`src/organize/organize_command.cpp:138-139`).

Constraints that shape the approach:

- The return value is in place: `executeOrganize` already returns the vector;
  nothing in `execute.cpp` has to change.
- The copy failure used by the test must be provoked without touching the copy
  code. `copySafely` first copies into `<root>/organized/.cache/tmp/<name>.part`
  and then renames onto the destination, and `executeOrganize` creates the
  output root and the staging directory with an ignored `error_code`. A regular
  file at `<root>/organized` therefore makes every copy fail: both directory
  creations fail, the staging copy finds no parent, and each item lands in the
  error vector. That is a real write failure through the real copy path, on any
  platform, with no permission-bit or Windows-specific trick.
- A stop request is the only abort that changes control flow in the copy loop
  (`execute.cpp:76-79`); an error is never fatal there, which is what keeps the
  exit-code question open rather than implied.

## Goals / Non-Goals

**Goals:**

- A failed copy is visible in the report the command already prints, exactly
  once per failure, in the renderer's existing detail-line form.
- The exit-code decision is explicit in the spec instead of implied by an
  unwired vector.

**Non-Goals:**

- Re-making the folder table into a per-image copy-status view:
  `buildFoldersSection` counts assignments (every scanned image has exactly one
  folder), while `copied` counts successful copies. The table, the totals line
  and the new failure lines are read together; changing the table is a report
  redesign of its own.
- Adding a heading, count or re-formatting for the failure block (D3).
- Changing `execute.cpp`, the copy path, the cache or the job state.

## Decisions

### D1: Wire the existing vector, do not re-derive the failures

`runOrganize` assigns `.copyErrors = stats.errors` in the `ReportData`
aggregate. The alternative — building report strings in the pipeline from
something else, or replacing `ExecuteStats::errors` with a richer type — adds a
second owner for text that already exists in one place, and this report is the
failure line's only consumer. The vector is copied (the stats value is about to
die anyway); no move gymnastics are needed for a handful of failures.

### D2: A failed copy does not change the exit code

The exit code stays whatever the run would end with otherwise (0 for a
completed run); only the report grows. The alternative is to fail the run when
any copy failed, the way the video encode flow does
(`hasEncodingFailures(summaryItems) ? 1 : 0`, `src/video/video_process.cpp:417`).

The repository does not have one convention to copy: video exits non-zero for
any encode failure, while the picture flows keep 0 for a partial batch, list the
failures, and fail only when everything failed
(`src/picture/picture_process.cpp:604-622`). With the precedent split, the
deciding factors are:

- A failed copy is recoverable in place and loses nothing: the originals are
  untouched, the output tree is consistent, and re-running retries exactly the
  missing copies (the skip-existing check is a content-hash comparison), so a
  partially organized tree is a normal intermediate state rather than a failed
  deliverable.
- The capability's exit-code contract is narrow today: non-zero for an
  unusable target directory, missing models, and cancellation (130). Widening it
  changes what wrapper scripts see for a run that already reports its own
  outcome; that is a breaking change that deserves its own proposal and its own
  spec requirement, not a rider on a visibility fix.

The spec states the outcome so the decision is reviewable and a future change
that wants the other behaviour edits the requirement on purpose.

### D3: The renderer is not touched

`renderReport`'s loop already prints one line per entry with no heading, and its
two-space indentation matches `organize`'s other detail lines (the missing-model
lines under `models not found in ...`, `src/organize/organize_command.cpp:46`).
A `copy failures (N):` heading would be new formatting in a report that has one
list, invented for a fix that is otherwise one assignment.

## Risks / Trade-offs

- [The folder table keeps counting assignments, so a failed copy can appear in
  the table while `copied` does not count it] → The failure line names that same
  image's source and destination on the line after the totals, so the
  difference between "assigned" and "copied" is stated in the same block; the
  Non-Goals record the redesign this deliberately leaves alone.
- [A partially copied tree still exits 0] → Stated in the spec (D2) and pinned
  by the test, so the decision cannot drift silently; the report names every
  affected image for the user and for a log reader.
- [The failure line adds output a script parsing the report might not expect] →
  The lines are appended after the totals and start with two spaces like the
  command's other detail lines, so a parser keyed on the totals line is
  unaffected.

## Migration Plan

None: report text only, no persisted state, no configuration, no on-disk
artifacts. Rollback is reverting the commit.
