## Context

See proposal.md — Why. Current behavior: `jobstate::configMatches` (src/core/job_state.cpp) compares every field of `ConfigSnapshot` for exact equality, including `packOutput`. A state file written by a run with `packOutput=false` therefore never matches a later run with `--pack-output`, so `Store::initialize` discards it (or errors under `--resume-state`), forcing a full re-encode. The resume machinery downstream of matching (mergeTasks, normalizeExistingTask, needsExecution, the pack prompt in video_process.cpp) is already correct for the matched case and needs no changes.

## Goals / Non-Goals

**Goals:**
- Allow `packOutput=false → true` across runs while keeping every other field strictly equal.
- Reuse the existing resume path unchanged (state load, task merge, restore, packing prompt).
- Preserve strict rejection for `packOutput=true → false` and for any other field mismatch.

**Non-Goals:**
- No state-file schema change (version stays 1, no new fields).
- No change to archive-task planning or pack resume semantics.
- No general "fuzzy config matching" for other fields.

## Decisions

### D1: Make only the packOutput comparison directional in `configMatches`

Change the single expression `lhs.packOutput == rhs.packOutput` to a directional rule: match if `lhs.packOutput == rhs.packOutput`, or if the saved state has `packOutput=false` and the new run has `packOutput=true`.

- Rationale: minimal, localized diff in the one function every resume path funnels through (`initialize`). No new fields, no schema change. The "reverse is invalid" case (`true → false`) falls out naturally because a state containing archive tasks must not be resumed by a run that will never plan archive tasks — otherwise stale archive records would linger in the snapshot and be merged back in on a later pack run.
- Alternative considered: dropping packOutput from the comparison entirely — rejected, because it would let an encode-only run resume a pack-enabled state, orphaning archive tasks (unplanned tasks stay in the snapshot, and `mergeTasks` never reconciles them).

### D2: Keep restore/merge logic untouched

Once the config matches, `initialize` loads the snapshot and `prepareEncodeActions`/`runResumable` already skip Succeeded tasks (restored from state or from existing output files) and run only `needsExecution` tasks. The "All encodes already complete. Do you want to proceed with packing?" prompt (video_process.cpp) fires on the matched path automatically.

## Risks / Trade-offs

- [A saved state with `packOutput=false` may contain only encode tasks; matching it under `packOutput=true` is sound because archive tasks are planned per-run and their fingerprints cover the archive file + members] → No mitigation needed; this is the intended path.
- [Tests currently assert exact-match semantics] → Update/extend `configMatches` tests in tests/job_state_tests.cpp; existing tests that pass identical configs remain valid.
- [Edge: state saved mid-encode (no pack) resumed with pack — some encodes still Running] → Existing `normalizeExistingTask` converts Running → Interrupted and re-runs only those tasks; unchanged behavior.

## Migration Plan

N/A — no schema change; old state files remain readable and resume behavior only relaxes one comparison.

## Open Questions

None.
