## Context

See proposal.md for motivation. Current constraints that shape the approach:

- The pipeline is free-function based; state lives in `AppContext` (`ctx.toolchain`, `ctx.config`, `ctx.runtime.videoInfoCache`). Several long signatures re-derive values that `AppContext` already holds (`ffmpeg` path, cached video-info JSON, worker counts), while the remainder fall into recurring clusters (segment identity, quality request, progress plumbing).
- `EncodeConfig` already exists as a full config object; `buildSegmentEncodeConfig` is its single construction path, shared by production encodes and probe encodes with an invariant ("probe differs from production only in CQ and output path") asserted by tests.
- The deepest call sites nest at 4 control-flow levels (`if (!encodeOneSegment(...))` in `video_encode_runner.cpp`, triple-nested loops in `packer.cpp`) with call arguments at ~8-9 spaces of continuation indent. Design goal: never add a control-flow level at a converted call site.
- Code style: East const, trailing returns for complex types, designated initializers already used (`EncodeConfig{.ffmpegPath = ...}`, `TaskSpec{.id = ...}`), `eh::Result` + flat early-return is the established error idiom.

## Goals / Non-Goals

**Goals:**
- Reduce the longest signatures (8-12 params) to ≤4 where clusters are real, and eliminate parameters that are pure derivations of `AppContext`.
- Keep every refactor a pure shape change: same values, same control flow, zero nesting increase at call sites.
- Preserve the single-construction-path invariant of `buildSegmentEncodeConfig` unchanged.

**Non-Goals:**
- No class/object-oriented conversion of the pipelines (rejected: see Decisions).
- No new validation, factory, or `Result`-returning builders for the aggregates (rejected: they are the nesting-increase vector).
- No changes to CLI surface, logging, or externally observable behavior.
- No unification of the two progress-plumbing shapes into one abstraction (they differ; see Decisions).

## Decisions

### D1. Remove parameters that are pure `AppContext` derivations

Resolved inside the function using the exact same expression the call sites use today:

- `ffmpeg` → `ctx.toolchain.ffmpegPath.value_or(fs::path{"ffmpeg"})` (already the in-function pattern in `measurePoint` / `runProbeEncode`).
- `info` (video info JSON) → `ctx.runtime.videoInfoCache.find(inputPath).value_or(boost::json::value{})` (already the pattern in `probeSingleFile`).

Scope: only where the derived value has exactly one derivation in `AppContext`. Exceptions, verified per call site during implementation:
- `workerCount` on the preview side (`encodeAndScoreAllWindows` computes `clamp(maxParallelJobs, 1, windows.size())`) is a *derived-in-caller* value, not a pure derivation — it stays a parameter.
- Probe-side `workerCount` (always `max(1, config.maxParallelJobs.value_or(4))`, fixed in `runProbePhase`) may be resolved once at the top and still passed down, or read from `ctx.config` at the use site — decided per function during implementation, whichever keeps the diff flat.

**Alternatives considered:** keep threading values for explicitness (rejected: half the "too many params" problem is re-passing what `ctx` already holds; the codebase already mixes both styles, so resolving inside is not a new pattern).

### D2. Parameter objects as plain data aggregates, constructed with designated initializers only

Three aggregates, placed next to their primary consumer:

- `SegmentEncodeSpec` (in `encode_config.h`): `inputPath`, `segmentIndex`, `startUs`, `durationUs`, `tempOutputPath`. Collapses the segmented-encode identity cluster shared by `buildSegmentEncodeConfig` and `buildProbeSegmentConfig`.
- `EncodeProfile` (in `encode_config.h`): `outputFormat`, `videoCodec`, `crf`, `EncodeInputSettings` (preset/maxrate), `workerCount`. The "how to encode" cluster; identical values for probe and production, preserving the construction-path invariant (D4).
- `QualityRequest` (in `video_quality.h`): `ffmpegPath`, `originalPath`, `encodedPath`, `startUs`, `durationUs`, `originalVideoInfo`, `encodedHasLocalPts`. Collapses `measureSegmentQuality`'s signature; `runVmaf`/`runSsim` take `QualityRequest const&` plus their log/stats-file path.
- Progress plumbing: `ProbeProgress` (in `encode_probe.cpp`, file-local) bundles `progressCtx`, `slotBars`, `slotProgress`, `completed`, `updateOverall` for `buildProbeTasks`/`buildProbeTaskSpec`. Preview keeps `progressCtx`/`bar`/`windowBase` as a small `BarSlot`-style aggregate in `preview_process.cpp` — **two separate shapes, not one shared abstraction**: the probe side is a multi-slot collection with counters and an updater callback; the preview side is a single bar with a base offset. Forcing one type would add indirection for no shared behavior.
- `WindowBatchSpec` (in `preview_process.cpp`, file-local): `original`, `windows`, `plan`, `settings`, `probeRoot`. The per-input window-job cluster of `encodeAndScoreAllWindows`; the two AppContext-derivable params (`ffmpeg`, `info`) are resolved in-function per D1. With `BarSlot` absorbing progress, the remaining signature is `(ctx, WindowBatchSpec, BarSlot, windowEncodeFailed)` — 4 params.
- `packer`: `buildDirectoryPackPlan` / `packSourceEntryChunks` / `packAllFilesInDirectory` get modest regrouping only where fields are genuinely cohesive and reused (e.g. recursion + naming strategy + concurrency into one `PackRequest`-style aggregate); no forced structs for 7-param functions whose parameters are already homogeneous scalars/paths.

Every aggregate is a struct of already-available values: no constructor logic, no `validate()`, no factory function. Call sites construct inline with designated initializers exactly like `EncodeConfig`/`TaskSpec` today.

**Alternatives considered:**
- Full object-orientation (pipeline classes holding state as members): rejected — each pipeline is instantiated once per run, state already lives in `AppContext`; classes would move parameters into members (hidden dependencies, lifecycle questions) and guarantee nesting growth at instantiation sites. This is the "one class, one use" anti-pattern.
- Builder pattern for aggregates: rejected — YAGNI; there is no incremental configuration, values exist in full at every call site.

### D3. Zero nesting-increase rule

The refactor must never add a control-flow level at a converted call site. Enforced by construction:

- Aggregates are constructed flat (statement above the call, or inline as the argument) — no `if`, no `auto x = make...(); if (!x)`.
- No aggregate returns `Result`/`optional`; no call is wrapped in a new branch.
- Lambda capture lists shrink (e.g. progress plumbing captured as one `ProbeProgress&` instead of five variables) — lambda bodies keep their indentation.

Measure of success: max call-site indent in the touched files does not increase; multi-line argument blocks shrink (verified by diff review, not by a tool).

### D4. Preserve the single construction path invariant

`buildSegmentEncodeConfig` remains the only way to build a segmented `EncodeConfig`. The invariant test (probe vs production differ only in CQ and output path) continues to pass unchanged; `buildProbeSegmentConfig` becomes a thin adapter mapping its parameters onto `SegmentEncodeSpec`/`EncodeProfile`. No second construction path is introduced.

### D5. Mechanical shape changes only

No behavior edits: scoring math, probe CQ sequence, cache keys, pack grouping thresholds, and error messages are untouched. Test expectations are unchanged; only test call sites that call refactored signatures are updated mechanically.

## Risks / Trade-offs

- [Behavior drift while removing redundancy] → Each removed parameter is replaced in-function by the byte-identical expression the call site used; verified call site by call site, with existing unit + e2e suites (`xmake test-report`, `xmake test-parallel`) as the safety net.
- [Aggregates become "bags of everything"] → Aggregates are scoped to a single cluster with an explicit section in D2; any parameter that does not fit a cluster stays a plain parameter.
- [Diff size across 9 files invites review fatigue] → Tasks are split per module; each task is a self-contained compile+test checkpoint (aggregate + its call sites), keeping diffs reviewable.
- [Extraction into object-like types raising nesting at deep call sites] → D3 forbids construction helpers, factories, and wrapping branches; flat designated-initializer construction only.

## Migration Plan

- Implement per module in dependency order: `encode_config.h` (aggregates first) → `video_quality` → `encode_probe` → `video_encode_runner`/`video_batch_execution` → `preview_process` → `packer`. Each task compiles and runs the unit suite before the next starts.
- Rollback: `git revert` of the single implementation commit removes the refactor atomically (implementation + tests + tasks checkboxes land in one commit per AGENTS.md).
- Verification gates: `xmake fmt -k`, `xmake test-report` (unit), `xmake test-parallel`, e2e for preview/probe flows.

## Open Questions

None — cluster boundaries and per-function parameter mapping are resolved in D2/D1; remaining per-function details (e.g. whether probe-side workerCount is re-read or passed down) are local shape decisions that do not change the approach or task breakdown.