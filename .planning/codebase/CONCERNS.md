# Codebase Concerns

**Analysis Date:** 2026-05-07

## Tech Debt

### `collision_naming.h` resides in `src/core/` but is a pack-internal concern

- Issue: The `collisionnaming` namespace (prefix generation, conflict-handled flat names, stable path hashing) is imported and used by `picture_process.cpp` (`src/picture/picture_process.cpp:5,22`), `video_output_planning.cpp` (`src/video/video_output_planning.cpp:3,12`), and pack-internal files. Despite Phase 17 (Picture Leak Elimination) being marked complete, both picture and video modules still directly call `naming::stablePathString()`, `naming::buildCollisionGroupPrefix()`, and `naming::buildConflictHandledFlatName()` to construct `PackEntryInput` structs.
- Files: `src/picture/picture_process.cpp`, `src/video/video_output_planning.cpp`, `src/core/collision_naming.h`, `src/pack/pack.cpp`, `src/pack/packer.cpp`, `src/core/job_state.cpp`
- Impact: Creates hidden coupling between consumers and naming implementation. Change to `stablePathString` case-folding or hash algorithm silently affects picture, video, and pack behavior across all consumers.
- Fix approach: Move `collision_naming.h` to `src/pack/`. All `PackEntryInput.sourceKey`/`fileKey` construction (currently done manually in picture/video) should be internalized inside `buildMediaPackPlan()` in `src/pack/pack.cpp`. Consumers would pass only file paths; the pack module assigns stable keys. Documented as research gap in `.planning/STATE.md:80`.

### Dual naming enum systems: `OutputLayout` (app_context.h) + `NamingStrategy` (pack.h)

- Issue: Two enums describe the same concept. `appctx::OutputLayout {Flat, Keep}` in `src/core/app_context.h:33-36` still exists alongside `pack::NamingStrategy {Flat, FlatWithForce, Keep}` in `src/pack/pack.h:45-49`. Consumers translate manually: `shouldForcePictureConflictNaming()` in `picture_process.cpp:45` and `shouldForceConflictNaming()` in `video_output_planning.cpp:16` both derive the enum mapping from `AppConfig.outputLayout` + `forceNameConflictHandling` boolean.
- Files: `src/core/app_context.h`, `src/pack/pack.h`, `src/picture/picture_process.cpp`, `src/video/video_output_planning.cpp`
- Impact: Single logical concept split across two enum systems. Any consumer touching both must maintain correct translation. Adding a 4th mode requires updating both enums and the translation logic.
- Fix approach: Deprecate `AppConfig::outputLayout` in favor of `NamingStrategy`, or make `NamingStrategy` the single source of truth and derive `OutputLayout` from it. The translation functions (`shouldForcePictureConflictNaming`, `shouldForceConflictNaming`) should be centralized in one location.

### `Packer` class has excessive public surface (14 public methods)

- Issue: `src/pack/packer.h` exposes 10 `group*`/`pack*` methods publicly, including implementation-internal grouping methods like `groupPreparedEntriesSequentially`, `splitSourceDirectoryEntries`, `packSourceEntryChunks`. The `remove-ipacker-abstraction.md` pending todo targets making these private after PackRequest migration, but they remain public.
- Files: `src/pack/packer.h`
- Impact: Consumers could bypass `pack::execute(PackRequest)` and call Packer directly. Makes the API surface larger than needed, complicating future changes to internal grouping algorithms.
- Fix approach: Move `groupPackFiles`, `groupPackEntries`, `groupPackEntriesWithSubparts`, `buildDirectoryPackPlan`, and all private static helpers out of the public interface. Only `packFilesToZip` overloads needed by PackService should remain public.

### Silent error returns with `return 0` masking failure information

- Issue: `video_process.cpp` uses `return 0` in 8 locations (lines 217, 253, 293, 315, 385, 427, 476, 494) within functions that return `int` exit codes. These returns bypass the `eh::Result` error propagation and discard error context. Similarly, `picture_process.cpp` uses `return 0` in multiple locations (lines 296, 338, 362, 461).
- Files: `src/video/video_process.cpp`, `src/picture/picture_process.cpp`, `src/pack/pack_service.cpp:564`, `src/app/app_entry.cpp:54`
- Impact: When encoding/packing sub-operations fail, the caller receives exit code 0 (success) despite partial or complete failure. Makes debugging production issues difficult since no error is logged or propagated.
- Fix approach: Convert these functions to return `eh::Result<int>` or `eh::Result<void>` to force explicit error handling. At minimum, log a warning before returning 0 on failure paths.

### `PackRequest` approaching field-count warning threshold

- Issue: `PackRequest` struct in `src/pack/pack.h:74-91` has 12 fields, 4 of which are `std::optional`. The PITFALLS.md research warns: "PackRequest fields exceeding 15" and "More than 3 levels of `std::optional` nesting" are danger signs. Currently at 12 fields with single-level optionals, but `NamingConfig` itself has 3 fields with one optional, creating effective 2-level nesting.
- Files: `src/pack/pack.h`
- Impact: Combinatorial validation complexity — not all field combinations are valid (e.g., `SummaryConfig` with `PackMode::Directory`, `recursive` with `PackMode::Media`). Invalid combinations are silently ignored or produce unexpected results.
- Fix approach: Add a `PackRequest::validate() -> eh::Result<void>` method that rejects invalid field combinations. Group related fields into sub-structs (already done with `NamingConfig`, `SummaryConfig`). Consider using `PackMode` to scope which fields are applicable.

### `entryNameForFile` callback is dead API surface

- Issue: The `entryNameForFile` callback on `PackRequest` (`src/pack/pack.h:89-90`) was designed for consumer-specific zip entry name overrides. Zero consumers use it — all current consumers pre-compute names into `PackEntryInput.entry.zipEntryName` before building `PackRequest`.
- Files: `src/pack/pack.h`, `src/pack/pack.cpp`
- Impact: Dead code that must be maintained, tested (standalone compile test exists in `tests/pack_api_standalone_compile_test.cpp`), and considered when changing the entry pipeline. Adds cognitive load with no value.
- Fix approach: Either find a real consumer (potential future use) or remove the callback and its associated code paths. If kept for future use, document the intended consumer and timeline.

### Inconsistent `return {}` pattern for error masking

- Issue: Throughout the codebase, `return {}` is used in functions returning `std::optional<T>`, `eh::Result<T>`, or `std::vector<T>` to indicate failure/empty conditions. While idiomatic, the lack of logging at these return points makes root-causing failures difficult: 98 occurrences across source files.
- Files: `src/video/video_info.cpp` (10 occurrences), `src/pack/packer.cpp` (4), `src/picture/picture_process.cpp`, `src/cmd/config_builder.cpp`, `src/pack/pack_service.cpp`, `src/core/media_scanner.cpp`
- Impact: Most `return {}` sites have no associated log message. When a pipeline fails deep in the call stack, the user sees a generic "failed" message with no indication of which `return {}` was the trigger.
- Fix approach: Add `spdlog::warn` or `spdlog::error` at key `return {}` sites, especially those involving filesystem operations or external tool invocations.

## Known Bugs

### `forceConflictHandling` default semantics gap between `AppConfig` and `NamingStrategy`

- Symptoms: `AppConfig.forceNameConflictHandling` defaults to `true` (`src/core/app_context.h:45`). But `NamingStrategy::Flat` (without `FlatWithForce`) produces non-conflict-handled names. If a consumer constructs `NamingConfig{.namingStrategy = NamingStrategy::Flat}` without explicitly checking `AppConfig.forceNameConflictHandling`, conflict handling is silently disabled. Currently all consumers manually translate via `shouldForcePictureConflictNaming()`/`shouldForceConflictNaming()`, but future consumers could miss this.
- Files: `src/core/app_context.h:45`, `src/pack/pack.h:45-49`, `src/picture/picture_process.cpp:45-48`, `src/video/video_output_planning.cpp:16-20`
- Trigger: Adding a new consumer that sets `NamingConfig::namingStrategy` without the force-check translation.
- Workaround: Current consumers correctly translate. No active bug, but the design invites it for future consumers.

### `PackRequest::compact = true` default overrides user intent if not explicitly set

- Symptoms: `PackRequest::compact` defaults to `true` (`src/pack/pack.h:80`). If a consumer constructs `PackRequest{}` without setting `.compact = !config.fullProgress`, packing always uses compact mode regardless of `--full-progress` flag. This was previously a v1.0 audit BLOCKER (`selectPackPlanIndexes` dropping `.compact`) — fixed, but the structural default remains fragile.
- Files: `src/pack/pack.h:80`, `src/pack/pack.cpp`
- Trigger: Any new consumer that constructs `PackRequest` without deriving `.compact` from `AppConfig.fullProgress`.
- Workaround: All current consumers correctly set `.compact = !config.fullProgress`. Documented as the field's doc comment: "consumers derive from config.fullProgress".

### `packAllPicsToZip()` does not pass `SummaryConfig` or `groupingStrategy` to `PackRequest`

- Symptoms: `packAllPicsToZip()` (`src/picture/picture_process.cpp:492-580`) manually builds `PackEntryInput` entries but omits `SummaryConfig` from the `PackRequest`. Summary entries are built into `entryInputs` directly, bypassing the `appendSummaryEntries` logic in `pack.cpp:179-200`. This means summary entries could theoretically be reordered if grouping strategy changes, since they lack the `isSummary = true` field on the outer `PackEntryInput` (it's set only on the inner `PackFileEntry`).
- Files: `src/picture/picture_process.cpp:492-580`
- Trigger: If grouping strategy changes from `PerSourceDirKeepTogether` to something else in a future refactor.
- Workaround: Currently works because entries are pre-sorted (summaries added first in the loop at lines 517-531). No active bug.

### `parallel::runIndexedTasks` creates new thread pool per call

- Symptoms: Every invocation of `parallel::runIndexedTasks` (`src/core/parallel.cpp:9-27`) constructs a new `BS::pause_thread_pool`, queues tasks, waits, and destroys the pool. For pipelines that call `runIndexedTasks` multiple times (e.g., encode batch → pack batch), thread creation/destruction overhead is repeated.
- Files: `src/core/parallel.cpp`
- Trigger: Frequent calls to parallel operations in a single run (e.g., encoding many small video files, then packing them all).
- Workaround: Not currently a practical bottleneck — thread pool construction is amortized by the work each thread does. But limits theoretical throughput for very small operations.

## Security Considerations

### External process execution without input sanitization

- Risk: `utils.cpp` executes external processes (FFmpeg, FFprobe) with user-provided file paths as command-line arguments. Paths containing shell metacharacters (spaces, quotes, semicolons) could cause command injection or unexpected behavior. `src/utils/utils.cpp:137-240` constructs command lines from `AppConfig` paths.
- Files: `src/utils/utils.cpp`, `src/video/video_encode_runner.cpp`, `src/video/video_info.cpp`, `src/video/encode_config.h`
- Current mitigation: Uses `boost::process::child` with argument vector, not shell string concatenation. C++26 `std::filesystem::path` provides some normalization. However, no explicit sanitization of path characters is performed.
- Recommendations: Validate that input paths do not contain control characters or shell metacharacters. Quote all paths passed to external processes. Consider using `boost::process::child` with explicit argument lists (already partially done) rather than command strings.

### Mixing C `malloc`/`free` with C++ memory management

- Risk: Several files use `std::free()` to deallocate memory obtained from C APIs (`_dupenv_s` on Windows). Mixing allocation strategies creates risk of mismatched allocators if the code is later refactored. Files: `src/infra/console_width.cpp:31`, `src/infra/terminal.cpp:51`, `src/app/prelude.cpp:33`, `tests/infra/console_width_tests.cpp:20`.
- Files: `src/infra/console_width.cpp`, `src/infra/terminal.cpp`, `src/app/prelude.cpp`
- Current mitigation: Allocations correctly paired (C API → C free). No known mismatches.
- Recommendations: Wrap these patterns in RAII types (e.g., a `unique_cstr` or similar) to prevent future mismatches. Currently deferred because the allocation sites are minimal and well-bounded.

### Spdlog raw pointer access without null check in crash handler

- Risk: `src/infra/crash_runtime.cpp:30-31` calls `spdlog::default_logger_raw()` and checks for `nullptr`. This is correctly handled, but the raw pointer bypasses spdlog's registry locking. If the logger is being destroyed concurrently with a crash, this could race.
- Files: `src/infra/crash_runtime.cpp`
- Current mitigation: Crash handler is called during abnormal termination (signals, exceptions). Race window is extremely narrow. The `try/catch(...)` at line 33-37 provides a fallback to `fwrite(stderr)`.
- Recommendations: Acceptable for crash handling (correctness > elegance during crashes). No change needed.

## Performance Bottlenecks

### `buildMediaPackPlan` copies large entry vectors between grouping stages

- Problem: `partitionPackInputs()` in `src/pack/pack.cpp:218-265` calls `Packer::groupPackEntriesWithSubparts()`, receives partitions, then copies them into new `std::vector<std::vector<PackFileEntry>>` groups. Each `PackFileEntry` contains `std::string zipEntryName` and `fs::path sourcePath` — these are heap-allocated strings/paths that get copied.
- Files: `src/pack/pack.cpp:218-265`, `src/pack/packer.cpp`
- Cause: The two-layer partitioning (partitions → groups) creates intermediate data structures. Partitions are already `PackEntryPartition` which contains `std::vector<PackFileEntry> entries` — the copy from `partition.entries` to `groupedEntries` is a full deep copy of all strings.
- Improvement path: Move entries from partitions instead of copying (use `std::move(partition.entries)`). The partitions vector is local and not reused afterward.

### `job_state.cpp` (653 lines) — large JSON serialization overhead per flush

- Problem: `jobstate::Store` serializes the full job state to JSON on each flush (2-second interval). For directories with thousands of files, this means serializing all task records repeatedly. The `toJson()` and `taskRecordToJson()` functions in `src/core/job_state.cpp:380-446` walk every task record.
- Files: `src/core/job_state.cpp`, `src/core/job_state_detail.h`
- Cause: Full-state serialization rather than incremental journaling. Every `markSucceeded`/`markRunning` call triggers the same O(n) JSON serialization of all records.
- Improvement path: Use an append-only JSON lines journal format instead of full-state rewrite. Or batch updates and serialize only changed records.

### Thread pool not reused across parallel operations

- Problem: Each call to `parallel::runIndexedTasks` (`src/core/parallel.cpp:9-27`) constructs a fresh `BS::pause_thread_pool`. For video encoding (1 call for encoding batch, 1 call for packing batch), this is negligible. But for picture compress+pack with many small tasks, thread pool creation overhead adds up.
- Files: `src/core/parallel.cpp`
- Cause: `BS::pause_thread_pool` is a simple wrapper; no built-in pool reuse mechanism exists.
- Improvement path: Consider a singleton or dependency-injected thread pool for long-running operations. Low priority — current overhead is small relative to I/O-bound encoding work.

## Fragile Areas

### Summary entry ordering in zip archives

- Files: `src/picture/picture_process.cpp`, `src/pack/pack.cpp:176-200,249-265`, `src/pack/packer.cpp`
- Why fragile: Summary entries (folder cover images) must appear first in every zip archive. Previously enforced by fragile `"0000__"` lexicographic prefix convention. Now enforced by `isSummary` boolean flag with `std::stable_partition` but depends on `buildMediaPackPlan` correctly preserving the invariant through multiple grouping stages.
- Safe modification: When changing grouping or partitioning logic, run the full picture test suite with summary enabled. Verify zip entry ordering in golden tests.
- Test coverage: `tests/app/pipeline_picture_tests.cpp` covers the picture pack workflow but does not explicitly assert summary-first ordering in zip output. The `naming_strategy_test.cpp` verifies naming patterns but not ordering.

### Resumable job state compatibility — zip name stability

- Files: `src/pack/pack.cpp:70-102` (`makeDefaultZipNameStrategy`), `src/core/job_state.cpp`, `src/core/job_state_store.cpp`
- Why fragile: Resumable execution (`--resume` flag) matches archive tasks by zip filename. If `buildPackZipBaseName`, `appendOrdinalRangeSuffix`, or `buildGroupOrdinalRanges` produce different zip names for the same inputs, previously-completed archives are unrecognized, causing redundant re-packing and orphaned state entries.
- Safe modification: Any change to zip naming logic MUST be accompanied by a golden test that verifies zip names match the previous version. The zip name format `{baseName}_part{X}.{Y}.zip` with ordinal range suffix must remain deterministic.
- Test coverage: `tests/pack_execute_test.cpp` tests resumable execution. No explicit golden test for zip name stability across build versions.

### Two-layer partitioning in `buildMediaPackPlan`

- Files: `src/pack/pack.cpp:218-265`, `src/pack/packer.cpp` (`groupPackEntriesWithSubparts`)
- Why fragile: Picture's two-layer partitioning (logical buckets → physical groups) is the most complex grouping behavior in the system. It depends on `GroupingStrategy::PerSourceDirKeepTogether` mapping to `keepSourceDirsTogetherWhenTotalFilesExceed = 0` which has specific semantic meaning ("never split a source directory across packs"). If this mapping changes, picture archives split incorrectly.
- Safe modification: When changing grouping logic, compare `groupPackEntriesWithSubparts` output for the same picture test directory before and after changes. Verify group/partition counts are identical.
- Test coverage: `tests/packer_tests.cpp` covers grouping. `tests/pack_execute_test.cpp` covers `buildMediaPackPlan`. No dedicated picture grouping test with real test directory.

### `AppConfig::outputLayout` / `NamingStrategy` translation logic is duplicated

- Files: `src/picture/picture_process.cpp:45-48` (`shouldForcePictureConflictNaming`), `src/video/video_output_planning.cpp:16-20` (`shouldForceConflictNaming`)
- Why fragile: Two identical (but subtly different) functions translate `AppConfig` to naming strategy. Video's version adds `&& (config.outputFormat != "mp4" || config.packOutput)` condition that picture's version lacks. If naming strategy logic changes, both functions must be updated consistently.
- Safe modification: Extract a single `resolveNamingStrategy(AppConfig const&, std::string_view consumerType)` function that handles both picture and video cases.

### `collision_naming.h` is all `inline` — any change triggers full recompilation

- Files: `src/core/collision_naming.h` (177 lines, all `inline` functions)
- Why fragile: Every function in `collision_naming.h` is defined `inline` in the header. Changing any function forces recompilation of all 5 translation units that include it (`pack.cpp`, `packer.cpp`, `job_state.cpp`, `picture_process.cpp`, `video_output_planning.cpp`).
- Safe modification: Move implementations to a `.cpp` file, keep only declarations in the header.

## Scaling Limits

### Max 2000 entries per logical partition

- Current capacity: `kMaxEntriesPerPart = 2000` (`src/pack/pack.cpp:104`)
- Limit: Directories with >2000 files per source directory get split across multiple partitions. Each partition becomes a separate zip archive. For picture packing, this means >2000 pictures per source directory creates multiple zip files.
- Scaling path: Increase `kMaxEntriesPerPart` — but this increases memory pressure during partition building and may hit zip format limits (65535 entries per archive). Currently tuned for typical use (2000 pictures per directory is very large).

### 500MB max archive group size

- Current capacity: `kDefaultMaxArchiveGroupSize = 500 * 1024 * 1024` (`src/pack/pack_types.h:47`, used in `packer.h:84`, `packer.cpp`)
- Limit: Groups of files exceeding 500MB total are split. For video encoding output (individual files often >100MB), 3-5 videos may fill one group. Very large videos may each get their own archive.
- Scaling path: Adjustable via `PackRequest` parameters (maxGroupSize is a parameter on grouping methods). Current default is reasonable for upload-friendly archive sizes.

### Thread pool size limited by `maxParallelJobs`

- Current capacity: `resolveWorkerCount()` (`src/core/task_executor.cpp:26-30`) caps workers at `max(taskCount, maxConcurrency)`, with no upper bound on `maxConcurrency`.
- Limit: If `--max-parallel-jobs` is set very high (e.g., 64) on a machine with fewer cores, thread oversubscription causes context-switching overhead. No hardware concurrency detection.
- Scaling path: `maxConcurrency` could be automatically capped at `std::thread::hardware_concurrency()`. Currently left to user discretion since encoding workloads benefit from more threads than cores (I/O-bound).

### Job state JSON file grows linearly with task count

- Current capacity: Each encode task adds one `TaskRecord` to the JSON state file. For 10,000 videos, the state file becomes several MB of JSON.
- Limit: No observed limit yet. JSON parsing/serialization is O(n) where n = number of tasks. Could become slow for very large directories (100k+ files).
- Scaling path: Migrate to SQLite or an append-only journal format for large-scale deployments.

## Dependencies at Risk

### `BS::thread_pool` (single-header, unversioned)

- Risk: `BS_thread_pool.hpp` is a third-party, single-header thread pool library included directly (not via xmake package). No version pinning. API changes in upstream could silently break compilation.
- Files: `src/core/parallel.cpp:3`
- Impact: Thread pool is core to all parallel operations (encoding, packing). A breaking change in the upstream header would require rewriting `parallel.cpp`.
- Migration plan: Pin to a specific commit hash via xmake package or vendor the header with a version comment. Alternatively, migrate to `std::execution` (C++26) or `boost::asio` thread pool.

### `libzippp` requires platform-specific toolchain configuration

- Risk: `libzippp` has complex Windows build requirements (`xmake.lua:41` — `toolchains=clang-cl` and a separate `libzip` config). Updates to the xmake package or libzippp could break the build configuration.
- Files: `xmake.lua:41-45`, `src/pack/packer.cpp:9`
- Impact: Zip packing is the pack module's core output format. If libzippp becomes unbuildable, the entire pack subsystem fails.
- Migration plan: Monitor libzippp upstream for breaking changes. Consider alternative zip libraries (minizip-ng, libzip directly) if maintenance burden increases.

### `immer` persistent data structures — complex template dependency

- Risk: `immer::vector` and `immer::atom` are used for immutable state in the video encoding path (`src/video/video_process.cpp:33`, `src/video/video_batch_execution.cpp`, `src/core/app_context.h`). It's a complex C++ template library with deep metaprogramming.
- Files: `src/video/video_process.cpp`, `src/video/video_batch_execution.cpp`, `src/core/app_context.h`
- Impact: Template instantiation errors from immer are notoriously difficult to debug. Compiler upgrades (clang-cl version changes) could break immer's template magic.
- Migration plan: `immer` is used for a specific pattern (immutable pending-video lists). Could be replaced with `std::vector` + careful copy semantics if immer causes issues.

### `boost::program_options` — heavy dependency for CLI parsing

- Risk: Boost is a large dependency. `boost::program_options` is only used for CLI argument parsing in `src/cmd/cmd.cpp` and `src/cmd/config_builder.cpp`. The full Boost dependency is pulled in (`boost[all]` in `xmake.lua:34`) even though only `program_options` and `json` are used.
- Files: `xmake.lua:34`, `src/cmd/cmd.cpp`, `src/cmd/config_builder.cpp`, `src/core/job_state.cpp`
- Impact: Long build times from Boost header parsing. Unnecessary dependency surface for a CLI tool.
- Migration plan: Replace `boost::program_options` with a lighter CLI library (CLI11, cxxopts) or C++26 reflection-based parsing when available. Replace `boost::json` with `nlohmann/json` or `simdjson` (though boost::json is already in use and working).

## Missing Critical Features

### No E2E picture tests (requires FFmpeg + test media)

- Problem: 8 E2E test paths are deferred from v1.3 because they require FFmpeg binaries and test image files not available in CI. Picture processing and packing are tested only through unit/integration tests with synthetic files.
- Blocks: Cannot validate the full picture compress → pack → zip pipeline end-to-end. Changes to picture processing could regress without detection.
- Deferred status: Documented in `.planning/STATE.md:97` as "requires test media + FFmpeg". No timeline for resolution.

### No coverage measurement infrastructure in CI

- Problem: xmake has a `coverage` mode (`xmake.lua:17-21` — `-fprofile-instr-generate -fcoverage-mapping`) but no CI step runs coverage or generates reports. Test coverage is tracked only by assertion counts (3033 assertions as of v1.5), not by line/branch coverage.
- Blocks: Cannot identify untested code paths. Large refactors risk breaking untested branches.
- Priority: Medium. Assertion count correlates with coverage in this well-tested codebase, but cannot guarantee branch coverage.

### `entryNameForFile` callback has no consumer

- Problem: The `entryNameForFile` callback on `PackRequest` (`src/pack/pack.h:89-90`) was designed for per-entry name customization but has zero production consumers. All consumers pre-compute names into `PackEntryInput.entry.zipEntryName`.
- Blocks: Maintaining unused API surface. The callback's code path in `buildMediaPackPlan` exists but is never exercised by real workloads.
- Priority: Low. Could be used by future third-party consumers of the pack module.

## Test Coverage Gaps

### Picture E2E pipeline (compress → pack → zip)

- What's not tested: The full `encro --process-type picture --pack` workflow with real image files and optional compression.
- Files: `tests/app/pipeline_picture_tests.cpp` (unit/integration level), `tests/e2e/encro_e2e_tests.cpp` (video-only E2E)
- Risk: Picture compression (via external tool) and packing could break silently without E2E verification.
- Priority: Medium. Mitigated by picture unit tests covering compress and pack logic separately.

### Summary entry ordering in final zip output

- What's not tested: No test explicitly asserts that summary entries appear first in the produced zip archive's entry listing.
- Files: `tests/app/pipeline_picture_tests.cpp`, `tests/pack_execute_test.cpp`
- Risk: Refactoring grouping/partitioning logic could reorder summary entries. Would silently break picture archives (cover images no longer at first position).
- Priority: High. This was a known pitfall (PITFALLS.md P1). Add a test that reads the produced zip's entry list and verifies `isSummary` entries come first.

### `forceConflictHandling` mapping from `AppConfig` to `NamingConfig`

- What's not tested: No test verifies that `forceNameConflictHandling = true` in `AppConfig` produces `NamingStrategy::FlatWithForce` in the constructed `PackRequest` for all consumers.
- Files: `tests/cmd_config_builder_tests.cpp` (only tests `AppConfig` parsing, not the naming translation)
- Risk: Future consumer could miss the translation step and silently disable conflict handling.
- Priority: Medium. Add a test that constructs `AppConfig` with `forceNameConflictHandling = true` and `outputLayout = Flat`, routes through `buildAppConfig` → `runAppPipeline`, and asserts the resulting zip entries use conflict-handled names.

### Video output planning with conflict handling and non-mp4 formats

- What's not tested: `shouldForceConflictNaming()` in `video_output_planning.cpp` has a special condition `config.outputFormat != "mp4" || config.packOutput` — this combination is not explicitly tested.
- Files: `tests/video/video_output_planning_tests.cpp`
- Risk: Non-mp4 formats combined with packing could produce incorrect output filenames if this condition logic changes.
- Priority: Low. Add a parameterized test exercising all output format * pack flag combinations.

### `parallel::runIndexedTasks` with empty task list

- What's not tested: The edge case where `taskCount == 0` or `!task` (null callback) — handled by early return but no test verifies this path doesn't crash or leak.
- Files: `src/core/parallel.cpp:14`
- Risk: Low. Simple guard clause. But `BS::pause_thread_pool` constructed with 0 workers could have unexpected behavior.
- Priority: Low. Add a trivial test calling `runIndexedTasks(0, 4, nullptr)` and verifying it returns without error.

---

*Concerns audit: 2026-05-07*
