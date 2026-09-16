## Why

The default (compact) packing progress line has two defects in the same indicator, both visible in the tail of a run:

1. **The status text flips.** The finalizing indicator and the packing status write to the single postfix slot of one bar, each gated on the opposite side of a counter that spans all in-flight archives (`src/pack/pack_service.cpp:110-160`). Every archive's `zip.close()` window crosses that counter's zero, so while several archives are finishing the line alternates between `Finalizing <frame>` and `Packing: archive k/N [file n/m]`, losing the label and the file counter each time.
2. **The frame spins as fast as the terminal repaints.** The indicator loop lost its 120 ms cadence in `7d8c626` ("refactor: flatten help column computation and pack spinner loop"): the wait moved into the branch taken only while no archive is finalizing, so the rendering branch has no delay and the animation frame advances once per repaint. With bars disabled (non-TTY stdout, `--quiet`) the loop stops rendering but keeps spinning, paying a console-size syscall per iteration.

The intended 120 ms cadence survives only in a test comment (`tests/pack_service_tests.cpp:258`) and in the full-progress spinner (`src/pack/packer.cpp:344-361`), which is correct because its wait is unconditional.

## What Changes

- The compact packing line becomes **one composed status**: the packing label (`Packing: archive k/N [file n/m]`) is always shown, and the finalizing indicator is appended as a separate ` | ` part while any archive is being finalized. The indicator never replaces the label, and no writer is ever suppressed, so the line stops flipping.
- The animation frame becomes a **function of elapsed time** (120 ms per frame, 4-frame cycle) instead of a counter incremented per repaint: repaint rate can no longer change animation speed, and progress updates inside a frame interval cannot advance the frame.
- The **repaint cadence is restored and made unconditional**: one indicator repaint per 120 ms while finalizing, plus the immediate wake-on-stop behaviour that already exists.
- The indicator **does not run when nothing can display it** (bars not renderable and no status-text consumer installed), so headerless/quiet runs stop paying for a timing thread.
- A **test seam** is added so the finalizing window can be held open deterministically: an optional pre-close hook on the pack plan, null in production.
- Existing regression test for the compact sequence is updated to the new text shape; new tests cover label survival, cadence bounds, frame stability, and the start condition.

Explicitly not changing: the `--full-progress` packing path (already correct), packing grouping/naming/exit codes/job-state semantics, postfix scroll rules, and pipeline narration.

## Capabilities

### New Capabilities

- `pack-progress-status`: what the compact packing progress line shows while archives are finalized, how the finalizing indicator animates, and when that indicator runs at all.

### Modified Capabilities

(none) — the existing `progress-scroll-label` requirements (postfix ` | ` grammar, scroll window, repaint timer) already hold for the new text shape and are not being changed.

## Impact

- `src/pack/pack_service.cpp` — compact progress state: composed postfix, time-derived frame, spinner loop cadence, start condition.
- `src/pack/pack_plan_internal.h`, `src/pack/packer.h`, `src/pack/packer.cpp` — optional pre-close hook carried from the plan into the archive close step (test seam; production callers leave it null).
- `src/core/progress.h`, `src/core/progress.cpp` — expose the existing "bars renderable" predicate (currently an anonymous-namespace helper) for the start condition.
- `tests/pack_service_tests.cpp` — update the finalizing-frame filter of the existing compact-sequence test; add the new cases.
- No CLI, config, file-format, or documentation impact: the packing status text is not documented outside the specs and its format (labels, ` | ` grammar) is unchanged.
