# Proposal

## Why

encro today processes exactly one input tree per invocation: a directory run scans, encodes, and packs the whole tree into part-numbered archives with no per-subfolder separation. Collections organized as one folder per release (for example, an author's works split by month) must currently be processed by hand-written shell loops, which lose per-subfolder resume, a unified overview, uniform archive placement, and a single failure summary. A first-class batch mode makes "each first-level subfolder is its own encro task" a supported product behavior instead of a script the user maintains.

## What Changes

- Add a `batch` subcommand: `encro batch <root> [processing options…]` treats every first-level subdirectory of `<root>` as an independent task and runs the existing single-directory pipeline over each one sequentially.
- One task type per batch invocation: `--type video` or `--type picture` (with pack/transcode flags) applies uniformly to every subfolder; no per-subfolder option variation. The skip pre-check mirrors the effective option combination (a video-only subfolder is processable under `--video-webp`; a pack-only batch considers any file processable).
- Per-subfolder output layout: archives land under `<root>/packed/<subfolder>/` by default; `-o/--output` overrides the packed root (default `<root>/packed`) as a plain directory path — the single-run output-alias forms (`+`, `=`, `input://`, `common://`) are rejected for batch. Archive naming inside each subfolder follows the existing single-run conventions.
- Per-subfolder resumable state: each subfolder gets its own job-state file under its own `.encro` work root — created exactly when the equivalent single-directory run would create one — so re-running the batch resumes each subfolder independently using the existing state matching and mismatch-discard semantics.
- Batch-level orchestration semantics: subfolders with no processable media are skipped with a notice (not failures); a failed subfolder does not stop the rest; a stop request halts after the current subfolder's safe point and marks the rest skipped; the run ends with a per-subfolder summary table and a non-zero exit code when any subfolder failed.
- Task enumeration excludes hidden directories, `.encro` work roots, the packed output root, and non-directory entries (loose files at the root are ignored).

## Capabilities

### New Capabilities

- `batch-subfolder-tasks`: batch orchestration over first-level subfolders — task enumeration and exclusions, one-task-type-per-run, per-subfolder independent pipeline execution and resumable state, per-subfolder output layout, failure isolation, stop semantics, and the batch summary table.

### Modified Capabilities

None. Existing capabilities cover the reused behaviors generically: `shell-completion` requires completion of every registered subcommand (batch is covered by registration), `cli-positional-input` gives known subcommand names precedence over positional inputs, `console-output-conventions` governs the summary table's rendering, and `job-state-resume-matching`/`job-state` store semantics apply unchanged to each per-subfolder run.

## Impact

- `src/cmd/`: register the `batch` subcommand and its option set (mirroring the `organize` subcommand's registration pattern, plus explicit exclusion stubs so `-I`/`--state-file` and the output-alias forms fail with native-style errors); completion candidates follow automatically from registration.
- New batch driver module (under `src/app/` or a new `src/batch/`): enumerate first-level subfolders, build a per-subfolder `AppContext`, invoke `pipeline::run` sequentially, aggregate statuses, print the summary table.
- `src/core/app_context.h`: new optional `packOutputDir` config field.
- `src/video/video_output_planning.cpp`, `src/picture/picture_process.cpp`, and `src/app/pipeline.cpp` (`runPackOnly`): zip output resolution honors `packOutputDir` before the existing `outputPath/packed` logic (avoids a `packed/packed` double nest when batch redirects outputs; all three call sites, not two).
- `src/picture/picture_process.cpp` / `src/video/video_info.cpp`: export the extension-based scan predicates the driver's media presence pre-check mirrors (today module-private / function-local).
- `src/core/job_state.h`/`.cpp`: `ConfigSnapshot` records the optional `packOutputDir` (absent when unset) so a subfolder's saved state binds to its output location without invalidating pre-existing snapshots.
- `src/app/app_entry.cpp`: dispatch branch for the batch subcommand.
- Tests: fake-tool unit tests for the driver (enumeration, exclusions, skip, failure isolation, stop-between-subfolders, summary) plus e2e coverage for the process boundary; render-touching summary output covered by the debug-dump contract where applicable.
