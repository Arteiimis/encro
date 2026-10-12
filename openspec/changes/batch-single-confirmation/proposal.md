# Proposal

## Why

A batch asks the workflow's confirmation prompt once per subfolder, so a run over N subfolders needs N answers to the same question — the prompt belongs to the single-directory pipeline that batch invokes per task, and its answer (`yesToAll`) is part of each subfolder's own config. Answering it is also not decisive: a declined prompt returns the prompt-abort exit code (0 unless a stop was requested), so the batch records the declined subfolder as done, does no work for it, and asks again for the next one. A confirmation is one consent for the run; a batch should ask it once and treat a decline as the end of the batch. The `batch` subcommand also shipped without user-facing documentation — the README has a section for every other subcommand and none for batch — so this change documents it alongside the confirmation fix.

## What Changes

- Ask once per batch: the first subfolder that reaches a confirmation prompt asks the user; every later subfolder in the same batch reuses that answer without prompting. The stored answer is batch-level, so it also covers a later subfolder whose confirmation has a different question text (for example a resumed subfolder's packing question followed by a fresh subfolder's encode question).
- Declining stops the batch: the declining subfolder and every remaining subfolder are marked skipped (the workflow's existing decline notice prints once, and the remaining subfolders print the same kind of skip notice the stop path already prints), and the batch exits 0 — the exit code a declined single-directory run produces. The batch does not report a declined subfolder as done.
- The prompt text stays byte-identical to the single-directory question, and it keeps its position in the pipeline: one consent for the run, asked once, with no batch-specific wording.
- `--yes` keeps suppressing the prompt entirely, and single-directory runs keep today's behavior: one prompt, asked at the same point in the pipeline as before.
- No new CLI option, no new prompt, and no new prompt text; the confirmation keeps its timing and what runs before it (video probing still precedes it, as in a single-directory run).
- Document the `batch` subcommand in the README: a `### Batch processing (batch)` subsection under `## Usage` plus one Quick start example, covering the batch contract as the `batch-subfolder-tasks` spec defines it — positional root and one task type per run, task enumeration and its exclusions, the `<root>/packed/<subfolder>/` default with the `-o/--output` override and the rejected alias forms, the rejected option forms (`-I`/`--inputs`, `--state-file`) with the single-run validation rules, per-subfolder resume, media-less skips, failure isolation, stop behavior, the per-subfolder summary table and the exit-code rule, the once-per-run ffmpeg/ffprobe resolution, and this change's once-per-batch confirmation with `--yes` as the scripted mode. Documentation changes no behavior, so it leaves the delta spec untouched.

## Capabilities

### New Capabilities

None. The change adds behavior to the existing batch capability rather than a new capability boundary.

### Modified Capabilities

- `batch-subfolder-tasks`: adds a requirement that a batch asks a confirmation prompt at most once, shares the answer across its subfolders, and treats a declined confirmation as the end of the batch (declining subfolder and remaining subfolders skipped, exit 0).

The batch capability owns this rule, which is why the single-directory specs that mention a confirmation prompt (`video-encode-probing`'s encode plan, `job-state-resume-matching`'s resumed-packing question) are not listed as modified: their requirements describe a single-directory run, and the new batch requirement scopes the batch case explicitly instead of restating the rule in three places.

## Impact

- `src/utils/utils.h`/`.cpp`: a confirmation wrapper that stores the first answer and reuses it for later callers (today's `readUserIpt` keeps its signature and its single-run behavior).
- `src/core/app_context.h`: `RuntimeContext` carries the batch-shared answer; it stays empty for single-directory runs, so their behavior is unchanged.
- `src/picture/picture_process.cpp`, `src/video/video_batch_execution.cpp`, `src/video/video_process.cpp`: the three confirmation call sites route through the wrapper (the picture helper takes the context instead of the config alone).
- `src/batch/batch.cpp`: creates one shared answer per batch, hands it to each subfolder's context, and stops the batch with the remaining subfolders skipped when the answer is a decline.
- `README.md`: a new `### Batch processing (batch)` subsection under `## Usage` and one Quick start example — the batch subcommand had no user-facing documentation before this change.
- Tests: `tests/utils_tests.cpp` for the wrapper (reuse without prompting, first answer stored, no answer stored while a stop is pending, and a run without a shared slot asking again), `tests/batch/batch_tests.cpp` for the batch contract (one prompt for many subfolders, a decline skips the rest and exits 0, a stop that lands while the prompt waits keeping the stop path), driven through the existing `testutils::ScopedCinBuf` / `captureStdout` seams.
- Unchanged: `--yes` semantics, the decline notice contract in `cancellation-reporting`, the not-started state removal in `job-state-resume-matching`, per-subfolder state matching, and every single-directory prompt path.
