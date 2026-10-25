# Proposal

## Why

A batch asks its workflow confirmation once (`batch-subfolder-tasks`, "Confirmation is requested once per batch"), but everything printed around that prompt belongs to the single-directory pipeline of the first subfolder that reaches it: its scan line (`Found 12 picture(s) under <root>/a.`) and, in a video run, its probe plan. The user therefore reads "12 pictures" and then authorizes every subfolder in the batch — the reported symptom is a confirmation naming only the first folder's count, followed by work on all of them. The number is not wrong; it is scoped to the wrong thing. The prompt's scope is the batch, its context is one subfolder, and nothing on screen states the run's real size before the answer is given.

## What Changes

- Before the first subfolder starts work, a batch that will ask a confirmation prints one scope block to stdout: a header naming how many of the enumerated subfolders are processable and the total entry count, one indented row per processable subfolder with its own count, and a statement that the confirmation below covers the whole batch.
- The batch driver scans the enumerated subfolders once up front and reuses that result for both the block and today's skip decision; the boolean pre-check (`hasProcessableMedia`) becomes a count-returning pre-check, because "any" cannot supply the block's numbers.
- The block prints only when a confirmation will actually be asked. `--yes`, `--pack-only` and `--dry-run` runs print none, and neither does a batch whose subfolders all have nothing to process; `--quiet` suppresses it with the rest of the narration, while the prompt stays.
- Nothing else changes: the prompt's text, its position in the pipeline, the ask-once answer, the decline path, the per-subfolder skip notices, the end-of-run summary table, the exit codes, and every single-directory run.
- `README.md`'s batch section states that the single confirmation is preceded by the run's scope block.

## Capabilities

### New Capabilities

None. The scope statement is a batch behavior, and the batch capability owns the confirmation it precedes.

### Modified Capabilities

- `batch-subfolder-tasks`: adds a requirement that a batch states the run's scope before its single confirmation — per-processable-subfolder counts, the totals, and the statement tying the answer to the whole batch — and names the runs that keep printing nothing before the prompt.

## Impact

- `src/batch/batch.h`, `src/batch/batch.cpp`: the pre-check returns an entry count (with pack-only keeping its presence-only fast path), `runTasks` scans the enumerated subfolders once before the loop, prints the block, and reuses the counts for the skip decision instead of re-scanning per subfolder.
- `tests/batch/batch_tests.cpp`: the block's content, its position before the first subfolder's own narration, and the noun per run mode; the no-block cases (`--yes`, pack-only, dry-run, quiet, nothing processable); the four existing pre-check cases move to the count-returning pre-check.
- `README.md`: one sentence in `### Batch processing (batch)` — the confirmation is preceded by the scope block.
- Unchanged: the workflows' prompts and their text, `readUserIptOnce` and the ask-once contract, the per-subfolder skip notices, `printSummary`, `batchExitCode`, the once-per-run toolchain resolution, per-subfolder state, and every single-directory output.
- Out of scope, recorded here so the change does not drift into it: `--pack-only` ignoring `-r/--no-recursive` (`PackRequest::recursive` defaults to true and `runPackOnly` never passes the config value), and `-t picture -z` failing per subfolder instead of failing the batch up front. Both are pack-path defects that predate this change and need their own proposal.
