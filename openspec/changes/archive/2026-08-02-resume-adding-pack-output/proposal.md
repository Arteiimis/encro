## Why

When a directory is first processed with `--type video` without packing (`--pack-output` absent), the job state file records the completed webp encodes. Re-running the same command with `--pack-output` added currently fails to match the saved config snapshot (`packOutput` is part of `configMatches`), so the state is discarded: all videos are re-encoded from scratch before packing. Users who only forgot the pack flag must pay a full re-encode to get packing.

## What Changes

- Relax job-state config matching so a previous run **without** packing (`packOutput=false`) is considered compatible with a new run **with** packing (`packOutput=true`).
- The reverse direction stays strict: a run without packing must NOT resume a state file written by a run with packing (that state may contain archive tasks, and the new run will not plan them).
- Once matched, the existing resume logic is reused unchanged: completed encode tasks are restored from saved state/output files, and the existing "All encodes already complete. Do you want to proceed with packing?" prompt (or direct packing when encodes still need to run) applies.
- Config matching for all other fields (format, layout, recursion, paths, etc.) is unchanged.

## Capabilities

### New Capabilities

- `job-state-resume-matching`: Rules for which saved job-state configurations a new run may resume, including the directional packOutput compatibility rule.

### Modified Capabilities

<!-- None: no existing spec's requirements change. -->

## Impact

- `src/core/job_state.cpp` — `configMatches` packOutput comparison becomes directional.
- `tests/job_state_tests.cpp` — new unit tests for the directional matching rule (both directions, plus unchanged strictness for other fields).
- No changes to the store schema (state file format, version, task records) — the state file is read and resumed as-is.
