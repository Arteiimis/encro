## Context

See proposal.md — Why. Three facts shape the approach, all verified in the
current tree:

- The encode phase's layout rule lives inside `EncodingProgressState`
  (`src/video/video_batch_execution.h:103-131`): `createOverallBar` branches on
  `compact ? totalTasks > 1 : totalTasks > workerCount`, and `makeSlotBars`
  returns nothing when `compact && totalTasks > 1`. The probe phase
  (`src/video/encode_probe.cpp:842-854`) reimplements the bar layout with the
  full-progress shape hard-coded.
- `runProbePhase` builds its `progress::ProgressContext` as a local, and
  `ProgressContext` exposes no bar count (`src/core/progress.h:120-121` hands out
  the `indicators::DynamicProgress` manager, which offers only `push_back` and
  `operator[]`). On a non-TTY stdout — where the `[encode-probe]` cases run —
  nothing renders at all. So no existing test can observe the probe's bars.
- The encode phase is observable only because `EncodingProgressState` keeps
  `slots.barIndexes` and `counters.overallBarIndex` as members its tests read
  (`tests/video/video_batch_execution_tests.cpp:72` reads `slots.barIndexes`).

## Goals / Non-Goals

**Goals:**

- The probe phase's bar layout follows `fullProgress` exactly as the encode
  phase's does.
- One definition of the layout rule, and a test that goes red if the probe phase
  stops honouring the flag — the bug being fixed must be the thing the test
  catches, not a proxy for it.

**Non-Goals:**

- No change to what a bar displays (postfix text, ETA, colors, roles, labels).
- No change to compact packing or picture progress; those already read the flag.
- No PTY/ConPTY test harness for rendering.
- Not fixing the pre-existing all-cache-hit frozen-bar behaviour (see Risks).

## Decisions

**D1 — The two layout booleans become pure predicates in `core/progress.h`.**
Both phases already include `core/progress.h`. `showsOverallBar(totalTasks,
workerCount, compact)` and `showsSlotBars(totalTasks, compact)` hold the rule;
`EncodingProgressState`'s private methods call them, and the probe's bar
creation does too. Alternative considered: read `ctx.config.fullProgress` in
`runProbePhase` and inline the same two conditionals (~6 lines, no new
declarations). Rejected because the conditions would stay untestable where the
probe uses them and the next phase to grow bars would copy them a third time.

**D2 — The probe phase passes the batch's file count, not the post-cache task
count, to the layout predicates.**
`runProbePhase` sizes its slot bars from `resolveWorkerCount(taskVids.size(),
workerCount)` — the count *after* cache hits are removed — while the Overall bar
counts every file (`vids.size()`), cache hits included. The layout decision must
use `vids.size()`: a 100-file batch with 99 cache hits is still a multi-file
batch, and compact mode should show it the Overall bar alone. The post-cache
`slotCount` keeps its existing job of sizing the bars that do get created, so a
fully-cached batch creates no slot bars while still showing the Overall bar.

**D3 — `runProbePhase` takes an optional injected `ProgressContext`, so the flag
wiring itself is testable.**
`runProbePhase(ctx, vids, progress::ProgressContext* external = nullptr)` uses the
caller's context when given one and a local otherwise — the same shape
`taskexec::runTasks` already uses for `TaskPlan::progress`
(`src/core/task_executor.cpp:84-85`). The bar creation moves into
`createProbeBars(progressCtx, fileCount, workerCount, slotCount, compact)`,
declared in `src/video/encode_probe.h` (the header `tests/video/encode_probe_tests.cpp`
already includes), returning the created indices; `ProgressContext::barCount()`
(a size read of `bars_` under the existing mutex, beside `tickCount()`) makes the
result observable. `addBar` is not TTY-gated (`src/core/progress.cpp:321-328`), so
the count is readable on the non-TTY stdout the tests run under, and `bars_`
survives `eraseBars()` (`:455-465` clears rendered lines only), so a test can read
the count after `runProbePhase` returns.
Two tests result: one on `createProbeBars` directly (compact vs full layout for
given counts), and one that runs `runProbePhase` with an injected context and
`config.fullProgress` both ways, asserting the created bar count differs — the
latter goes red if `runProbePhase` ignores the flag, which is the bug.
Alternative considered: test only the predicates (D1) and accept that the call
site is covered by reading. Rejected because that is exactly the bug at hand,
and a test that cannot fail for it does not earn its place.
Stated honestly: this still does not pin the *count* choice (D2) when cache hits
make `taskVids.size() != vids.size()` — that stays covered by reading.

**D4 — `ProbeProgress.slotBars` may be empty, and the per-task body guards on
it.**
Compact mode with more than one file creates no slot bars, but the per-task
lambda still runs and would index `slotBars[slot]`
(`src/video/encode_probe.cpp:567`). The guard covers that line and both local
lambdas; `slotProgress[slot]` and `completed` keep updating unconditionally,
because the Overall bar's progress is computed from them
(`src/video/encode_probe.cpp:857-873`). `slot` is always a valid index into
`slotProgress` in both modes: `taskexec::runTasks` sizes its pool from
`resolveWorkerCount(plan.tasks.size(), maxConcurrency)` and hands each worker its
own slot number — the same count `runProbePhase` used to size `slotProgress`
(`src/core/task_executor.cpp:86-103`). Alternative considered: size
`slotProgress` to zero in compact mode and skip the overall-bar fold too —
rejected because it would make the compact Overall bar advance in whole-file
jumps instead of tracking in-flight work.

**D5 — Full-progress output stays byte-identical, and the single-file carve-out
stays.**
In full mode both predicates return what the probe phase already does
(`vids.size() > workerCount` for the Overall bar, one bar per worker slot).
`showsSlotBars` keeps the encode phase's carve-out: a one-file batch creates one
slot bar in both modes (`compact && totalTasks > 1` is false at one task), and
`showsOverallBar` is false there. Making a single-file probe create nothing would
be a second, unrequested behaviour change.

## Risks / Trade-offs

- [The count choice itself — `vids.size()` rather than the post-cache
  `taskVids.size()` when cache hits make them differ — is covered by reading, not
  by a test] → accepted: pinning it would mean seeding the probe cache in the
  test to create a divergence. The flag wiring, the layout rule, and
  `createProbeBars`'s honouring of its argument are all unit-tested.
- [Compact mode now shows fewer bars during probing than before] → intended: it
  matches what the encode phase already does for the same run, and matches the
  `-F` help text.
- [Pre-existing, NOT fixed: the Overall bar is created before the cache scan
  (`src/video/encode_probe.cpp:842` vs `:852`) and `updateOverall` is only ever
  called from task bodies, so an all-cache-hit run leaves a bar frozen at `0/N`
  until `eraseBars`. `completed` counts only tasks, so cached files never advance
  it either] → out of scope: this change does not alter that path, though compact
  mode widens the trigger condition from `vids > workers` to `vids > 1` and the
  bar is now the only bar in compact mode. The existing comment at
  `src/core/progress.h:140-141` shows the "added but never rendered" case was
  already known. Worth its own change; recorded here so it is not silently
  inherited as "fine".
- [A one-file probe creates a slot bar even in compact mode] → deliberate
  (D5): the encode phase does the same, and the predicate is the shared rule.
