# Code Reuse Optimization Plan

This plan is the working proposal for reducing repository-wide duplication and improving code reuse without changing current behavior.

## Goals

- Reduce obvious helper-level duplication across neighboring modules.
- Move repeated path, naming, and workflow logic behind shared abstractions with clear ownership.
- Improve test reuse so new coverage does not keep copying local helpers into large test files.
- Keep CLI behavior, output naming semantics, job-state behavior, and packing results unchanged.
- Lower the future probability of duplication reappearing in the largest files.

## Non-Goals

- Do not redesign the domain model or CLI surface.
- Do not change archive size limits, output formats, progress behavior, or resume semantics as part of this plan.
- Do not introduce generic abstractions unless they remove verified duplication immediately.
- Do not rewrite stable pure logic only for style consistency.

## Current Assessment

The repository already has good reuse at the infrastructure layer:

- shared error handling in `src/core/error_handle.h`
- shared collision naming in `src/core/collision_naming.h`
- shared parallel task execution in `src/core/task_executor.{h,cpp}`
- shared temporary-directory setup in `tests/test_utils.h`

The main remaining redundancy is concentrated in three places:

1. Neighboring workflow modules that re-implement small helpers instead of sharing them.
2. Cross-cutting path and planning logic that exists in both command parsing and execution layers.
3. Test files that define local helper structs/functions repeatedly instead of promoting them to shared fixtures.

## Highest-Priority Hotspots

### 1. Video helper duplication

Observed in:

- `src/video/video_process.cpp`
- `src/video/video_batch_execution.cpp`

Examples:

- duplicate `maybeJobState(...)`
- duplicate `lookupPlannedOutputFile(...)`
- repeated job-state null-check/update patterns

Why it matters:

- These files already sit in the largest and most change-prone part of the repository.
- Small helper duplication here increases drift risk when job-state or output-planning behavior changes.

### 2. Path-root and common-ancestor duplication

Observed in:

- `src/video/video_process.cpp`
- `src/cmd/config_builder.cpp`

Examples:

- `normalizeSourceRootDir(...)` vs `normalizeInputRootDir(...)`
- `commonAncestorPath(...)` vs `commonAncestorDir(...)`

Why it matters:

- Command parsing and runtime planning should resolve roots the same way.
- Keeping the logic split across layers risks silent divergence for multi-input behavior.

### 3. Pack planning helper duplication

Observed in:

- `src/pack/pack_service.cpp`

Examples:

- two identical `buildGroupOrdinalRanges(...)` overloads differing only by element type
- repeated max zip-size constant usage in pack-facing call sites

Why it matters:

- This is easy, low-risk duplication that should be removed first.
- The pack path is already a shared dependency for video, picture, and pack-only workflows.

### 4. Workflow-level orchestration overlap

Observed in:

- `src/app/pipeline.cpp`
- `src/picture/picture_process.cpp`
- video orchestration split across `src/video/video_process.cpp` and supporting modules

Examples:

- scan -> plan -> confirm -> execute -> summarize flow repeated with local variations
- pack-only and picture mode each carrying their own orchestration glue

Why it matters:

- This is not copy-paste duplication everywhere, but it is structural repetition.
- New features in one workflow are likely to be copied into the others unless the shape is normalized.

### 5. Test helper duplication

Observed in:

- `tests/app/pipeline_tests.cpp`
- `tests/video_process_tests.cpp`
- `tests/media_scanner_tests.cpp`
- `tests/cmd_config_builder_tests.cpp`
- `tests/e2e/encro_e2e_tests.cpp`

Examples:

- duplicated `ScopedStopSignalReset`
- duplicated `hasCollisionSafePrefix(...)`
- duplicated `collisionGroupPrefix(...)`
- duplicated file-write/touch helpers
- duplicated regular-file listing helpers

Why it matters:

- The test suite is large enough that local helper copying now has meaningful maintenance cost.
- This is the fastest way to reduce visible redundancy without touching behavior-sensitive runtime code.

## Execution Principles

- Remove the smallest verified duplication first.
- Prefer extraction to existing layers before creating new folders or concepts.
- Keep ownership explicit: every shared helper should have one obvious home.
- After each phase, validate only the narrowest affected test slice first, then expand if needed.
- If a shared helper would only have one caller after extraction, do not extract it yet.

## Execution Order

### Phase 0: Establish Guard Rails and Baseline

- [x] Record the narrowest build and test commands for each touched area.
- [x] Capture the largest files and main duplication hotspots in this document before edits begin.
- [x] When a phase touches orchestration behavior, add or preserve focused tests before moving logic.

Verified baseline on Windows:

- `xmake build tests`
- `xmake run tests "[pipeline]"`
- `xmake run tests "[cmd][config]"`
- `xmake run tests "[pack-service]"`
- `xmake run tests "[video-process][pack]"`
- `xmake run tests "[video-process][orchestration]"`

Current high-risk file-size baseline:

- `src/core/job_state.cpp` - 789 lines
- `tests/video_process_tests.cpp` - 758 lines
- `src/video/video_batch_execution.cpp` - 649 lines
- `src/pack/packer.cpp` - 632 lines
- `tests/app/pipeline_tests.cpp` - 487 lines
- `src/video/video_process.cpp` - 484 lines
- `tests/e2e/encro_e2e_tests.cpp` - 466 lines
- `tests/cmd_config_builder_tests.cpp` - 445 lines
- `src/video/video_info.cpp` - 349 lines
- `src/cmd/config_builder.cpp` - 331 lines

Confirmed duplication baseline before Phase 1:

- video-local helper duplication between `src/video/video_process.cpp` and `src/video/video_batch_execution.cpp` for job-state/output-lookup helpers
- shared-root/common-ancestor logic duplicated between `src/video/video_process.cpp` and `src/cmd/config_builder.cpp`
- identical ordinal-range helper overloads in `src/pack/pack_service.cpp`
- repeated test-only helper blocks in `tests/app/pipeline_tests.cpp`, `tests/video_process_tests.cpp`, `tests/media_scanner_tests.cpp`, and `tests/cmd_config_builder_tests.cpp`

Focused orchestration guard rails already present:

- `tests/app/pipeline_tests.cpp` exercises `pipeline::run(...)` through the `[pipeline]` tag
- `tests/video_process_tests.cpp` exercises `handlePathEncoding(...)` and `handleMultiFileEncoding(...)` through the `[video-process][orchestration]` tag

Validation:

- `xmake build tests`
- targeted test subsets per affected area

Exit criteria:

- Each planned refactor slice has a matching validation command.
- High-risk files have a known test surface before extraction begins.

### Phase 1: Remove Low-Risk Helper Duplication in Existing Modules

- [x] Deduplicate `buildGroupOrdinalRanges(...)` in `src/pack/pack_service.cpp` using a template or shared internal helper.
- [x] Deduplicate `maybeJobState(...)` between `src/video/video_process.cpp` and `src/video/video_batch_execution.cpp`.
- [x] Deduplicate `lookupPlannedOutputFile(...)` between the same two video modules.
- [x] Consolidate repeated local constants such as max zip-size where the policy is already shared.

Verified on Windows with:

- `xmake run tests "[pack-service]"`
- `xmake run tests "[video-process][orchestration]"`
- `xmake run tests "[pipeline]"`
- `xmake run tests "[video-process][pack]"`

Preferred destination:

- internal helper in the owning module if the reuse stays local
- `src/core` only if the helper clearly spans domains

Exit criteria:

- No exact helper duplication remains across neighboring video modules.
- No same-file template-free overload pair remains when the algorithm is identical.

### Phase 2: Centralize Shared Path and Root Resolution

- [x] Extract shared root/ancestor helpers used by command parsing and runtime planning.
- [x] Make multi-input base-path resolution use one canonical implementation.
- [x] Keep alias semantics unchanged for `+`, `input://`, `=`, and `common://`.
- [x] Add or keep tests for multi-input shared-parent and common-ancestor behavior.

Verified on Windows with:

- `xmake run tests "[cmd][config]"`
- `xmake run tests "[video-process][orchestration]"`

Candidate ownership:

- `src/core` if these helpers remain domain-neutral
- otherwise a narrow internal header under `src/cmd` or `src/video` that both sides can include intentionally

Exit criteria:

- Root normalization and common-ancestor logic exist in one place.
- Multi-input path behavior is defined once and consumed by both parse-time and run-time logic.

### Phase 3: Normalize Job-State Access and Small Shared Workflow Primitives

- [x] Replace scattered `if (auto* store = ...; store != nullptr)` patterns where a tiny wrapper can reduce repetition without hiding control flow.
- [x] Introduce a minimal helper for common job-state stage update or cancel-mark patterns if the call shape is truly repeated.
- [x] Avoid building a broad abstraction; keep this phase intentionally small.

Verified on Windows with:

- `xmake run tests "[video-process]"`

Implementation note:

- `src/video/video_workflow_utils.h` now owns thin `withJobState(...)` and `withActionJobState(...)` helpers for short job-state side effects.
- Direct `maybeJobState(...)` access is intentionally retained only where `store == nullptr` changes control flow, such as `prepareEncodeActions(...)`.

Guard rail:

- Do not bury important behavior transitions behind opaque utility functions.
- Keep stage changes and failure handling readable at the call site.

Exit criteria:

- Repeated job-state access boilerplate is reduced.
- Stage transitions remain explicit in orchestration code.

### Phase 4: Consolidate Workflow-Orchestration Skeletons

- [x] Consolidate the picture scan -> plan -> optional confirm -> execute -> summarize flow into `src/picture/picture_process.cpp` so `src/app/pipeline.cpp` only dispatches.
- [x] Identify the smallest common shape across pack-only, picture, and video flows.
- [x] Extract only the stable orchestration steps that already repeat: scan, plan, optional confirm, execute, summarize.
- [x] Keep domain-specific decisions in the domain modules.
- [x] Do not force picture/video/pack-only into one generic framework if the shared code is only superficial.

Implementation note:

- `runPicturePackWorkflow(...)` now owns the picture-mode orchestration entry point.
- `packAllPicsToZip(...)` and the pipeline picture branch now share the same picture-local preparation and confirmation steps instead of duplicating them.
- `runDirectoryPackWorkflow(...)` now owns the pack-only orchestration entry point, so the pipeline only validates mode/input and dispatches.
- Video orchestration remains in `src/video` because the remaining control flow is domain-specific enough that a generic workflow abstraction would hide important behavior.

Recommended scope for this phase:

- shared confirmation helper
- shared summary/logging helper only if message semantics align
- shared pack execution entry helper only if it removes existing repetition immediately

Exit criteria:

- Repeated orchestration glue shrinks without introducing a generic workflow abstraction that is harder to follow than the original code.
- Pipeline-level picture and pack-only orchestration now delegate into their owning modules, leaving `src/app/pipeline.cpp` as a thin dispatcher.

### Phase 5: Promote Repeated Test Helpers to Shared Fixtures

- [x] Move duplicated test-only helpers out of `tests/app/pipeline_tests.cpp` and `tests/video_process_tests.cpp`.
- [x] Extend `tests/test_utils.h` or add a small shared test helper header for:
  - stop-signal reset guard
  - file touch/write helpers
  - collision-safe name assertions/parsers
  - regular-file listing helper where broadly reused
- [x] Keep e2e-only helpers under `tests/e2e` when they should not leak into unit tests.

Implementation note:

- `tests/test_utils.h` now owns shared `ScopedStopSignalReset`, `touchFile(...)`, `writeFile(...)`, `writeTextFile(...)`, `hasCollisionSafePrefix(...)`, `collisionGroupPrefix(...)`, and `listRegularFiles(...)` helpers.
- `tests/app/pipeline_tests.cpp`, `tests/video_process_tests.cpp`, `tests/media_scanner_tests.cpp`, `tests/cmd_config_builder_tests.cpp`, and `tests/job_state_tests.cpp` now consume the shared helpers instead of carrying local duplicates.
- e2e-specific process and tool-launch helpers remain under `tests/e2e`, while only domain-neutral filesystem/assertion helpers were promoted.

Verified on Windows with:

- `xmake run tests "[pipeline]"`
- `xmake run tests "[video-process]"`
- `xmake run tests "[cmd][config]"`
- `xmake run tests "[media-scanner]"`
- `xmake run tests "[job-state]"`

Exit criteria:

- Local helper duplication across large test files is materially reduced.
- New tests have an obvious shared fixture home instead of adding another local helper block.

### Phase 6: Reduce Growth Pressure in the Largest Files

- [x] Review the highest-growth files after Phases 1-5:
  - `src/core/job_state.cpp`
  - `src/video/video_batch_execution.cpp`
  - `src/pack/packer.cpp`
  - `src/video/video_process.cpp`
  - `tests/video_process_tests.cpp`
  - `tests/app/pipeline_tests.cpp`
- [x] Split only when a file still mixes more than one independent responsibility.
- [x] Prefer moving cohesive helper clusters, not scattering one-off functions.

Implementation note:

- `src/core/job_state.cpp` now keeps snapshot/persistence logic plus the public task helpers, while `src/core/job_state_store.cpp` owns `jobstate::Store` mutation/flush behavior via `src/core/job_state_detail.h`.
- `src/picture/picture_process.cpp` now also reuses `pack::kDefaultMaxArchiveGroupSize`, so the shared archive-size policy no longer diverges between picture, video, and pack-only flows.
- `tests/app/pipeline_tests.cpp` was split into `tests/app/pipeline_pack_only_tests.cpp` and `tests/app/pipeline_picture_tests.cpp`, and `tests/test_utils.h` now provides `listZipRegularEntryNames(...)` so the split does not recreate local zip-reading boilerplate.
- `tests/video_process_tests.cpp` was split into `tests/video/video_progress_parser_tests.cpp`, `tests/video/video_output_planning_tests.cpp`, and `tests/video/video_process_orchestration_tests.cpp` so parsing, planning, and orchestration coverage each have one obvious home.
- `src/video/video_batch_execution.cpp`, `src/video/video_process.cpp`, and `src/pack/packer.cpp` were reviewed and intentionally left intact for now because each file is currently centered on one workflow concern; splitting them further at this stage would mostly separate tightly coupled local flow rather than remove cross-cutting catch-all behavior.

Verified on Windows with:

- `xmake build tests`
- `xmake run tests "[job-state]"`
- `xmake run tests "[pipeline]"`
- `xmake run tests "[video-process]"`

Exit criteria:

- The largest files stop acting as catch-all locations for new logic.
- Future reuse points have clearer ownership boundaries.

## Suggested Ownership Map

- `src/core`
  - path-root helpers used by more than one domain
  - tiny job-state access helpers only if they are domain-neutral
- `src/pack`
  - pack grouping policy, ordinal naming, shared archive limits
- `src/video`
  - video-specific output lookup and encoding workflow helpers
- `tests/test_utils.h` or a nearby shared test header
  - unit-test fixtures and small helper functions reused across test files
- `tests/e2e`
  - process-launch and filesystem helpers that are specifically e2e-oriented

## Validation Commands

Use the narrowest command possible after each phase.

- Build tests: `xmake build tests`
- Run all unit tests: `xmake run tests`
- Run all e2e tests: `xmake run e2e_tests`

Prefer focused subsets when only one area changes, for example:

- `xmake run tests "[video-process]"`
- `xmake run tests "[pipeline]"`
- `xmake run tests "[pack-service]"`
- `xmake run tests "[cmd][config]"`

## Success Metrics

Track improvement qualitatively and with lightweight repository metrics.

- Fewer duplicated helper definitions across `.cpp` files.
- Fewer local helper blocks at the top of large test files.
- Fewer behavior-neutral constants duplicated across modules.
- Reduced growth in the largest workflow files.
- New changes naturally extend existing helpers instead of adding one more local variant.

## Done Definition

- Neighboring modules no longer carry obvious duplicate helpers for job-state, output lookup, and root resolution.
- Command parsing and runtime planning use one canonical root/ancestor implementation.
- Test helper duplication is substantially reduced across the large workflow-oriented test files.
- Shared abstractions remain small and concrete, not speculative.
- The repository has fewer places where the next similar feature would be copy-pasted instead of extended.
