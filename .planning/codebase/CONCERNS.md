---
last_mapped_commit: 45d19cdfd54971db03ee0758ccaad6c61aad4ff1
---
# Codebase Concerns

**Analysis Date:** 2026-05-22

## Tech Debt

**Revamped --dry-run feature fully reverted (commit daa0373):**
- Issue: A substantial three-layer dry-run pipeline was implemented across multiple commits (tests, CLI flag, validation, scan, plan) and then fully reverted in a single commit titled "revert(260519-1ym): undo --dry-run implementation". The implementation touched `video_process`, `video_batch_execution`, `cmd`, `config_builder`, `pipeline`, and `prelude`.
- Files: `src/app/pipeline.cpp`, `src/cmd/cmd.cpp`, `src/cmd/config_builder.cpp`, `src/video/video_process.cpp`, `src/video/video_batch_execution.cpp`, `src/app/prelude.cpp`, plus corresponding test files.
- Impact: The revert commit leaves behind no trace of the feature, but the effort represents significant wasted engineering time. The feature may be attempted again later without retaining the implementation lessons.
- Fix approach: If --dry-run is re-attempted, reference the reverted commits (c4675b9 through 14ee719) to avoid repeating design mistakes.

**Large files with concentrated logic:**
- Issue: Several source files exceed 500 lines, concentrating cross-cutting concerns in single translation units. `cmd.cpp` (824 lines) handles CLI parsing, flag registration, help formatting, and result population in one file. `packer.cpp` (804 lines) combines preparation, grouping, partitioning, and ZIP I/O. `job_state.cpp` (653 lines) mixes serialization, legacy migration, and lifecycle logic. `picture_process.cpp` (588 lines) and `pack_service.cpp` (562 lines) are also large.
- Files: `src/cmd/cmd.cpp`, `src/pack/packer.cpp`, `src/core/job_state.cpp`, `src/picture/picture_process.cpp`, `src/pack/pack_service.cpp`
- Impact: Harder to test isolated concerns. A change to help formatting requires recompiling the entire CLI module. Modifications risk unintended side effects in unrelated sections.
- Fix approach: Extract help formatting into `src/cmd/help_formatter.cpp`. Split packer grouping logic from ZIP I/O. Separate job_state serialization from business logic.

**Legacy state file format migration code:**
- Issue: `job_state.cpp` contains explicit "legacy" handling functions (`legacySourcePathsFrom()`, `legacyTargetPathsFrom()`, `inferLegacyKind()`) that bridge old and new state file formats. The JSON parser handles both `"archiveMembers"` and `"inputPath"` keys, and the task array can be under `"tasks"` or legacy `"actions"` keys.
- Files: `src/core/job_state.cpp` (lines 178-230)
- Impact: Increases maintenance burden for all state file code changes. New developers must understand the old format before modifying the new one.
- Fix approach: Document the last version that wrote each legacy format. After a suitable migration window, drop legacy field support and add a version check that rejects files older than a minimum version.

**No TODO/FIXME markers anywhere in the codebase:**
- Issue: A complete absence of TODO, FIXME, HACK, or XXX comments across all source and test files (0 matches in 30 source files and 37 test files). This suggests either the codebase is pristine, or more likely, that technical debt is not being documented inline.
- Files: All source files in `src/` and `tests/`
- Impact: Known shortcuts, pending improvements, and edge cases are not discoverable through code scanning. Onboarding developers cannot quickly identify areas needing attention.
- Fix approach: Encourage developers to use TODO comments for deferred improvements and FIXME for known bugs.

**Platform-specific code duplication in exec2Impl:**
- Issue: `exec2Impl()` in `src/utils/utils.cpp` has two completely separate implementations: a Windows path (~138 lines) using `bp::pipe` + `PeekNamedPipe`, and a POSIX path (~110 lines) using `bp::ipstream` + `std::thread`. The two paths share no code.
- Files: `src/utils/utils.cpp` (lines 27-278)
- Impact: Bug fixes or behavior changes must be applied to both implementations. Divergent behavior is likely between platforms.
- Fix approach: Extract common logic (command building, stop-signal handling, timeout constants, error formatting) into shared helpers. Consider a platform-abstraction base with platform-specific subclasses.

**External clang-format config dependency:**
- Issue: The clang-format style file is hard-coded to `D:/clangformat/.clang-format`, an absolute path outside the repository. Both the pre-commit hook (`.githooks/pre-commit`) and the `xmake format` plugin reference this path.
- Files: `.githooks/pre-commit` (line 3), plugin at `plugins/format/`
- Impact: New contributors cannot run formatting checks without manually creating this external file. CI cannot run format checks without replicating the file. The formatting configuration is not version-controlled.
- Fix approach: Move `.clang-format` into the repository root and reference it via `--style=file` (relative path). Update the pre-commit hook and xmake plugin to use `$(git rev-parse --show-toplevel)/.clang-format`.

**Heavy boost dependency -- boost[all] pulls entire boost suite:**
- Issue: The xmake.lua requests `boost[all]` but only uses a subset: json, process, stacktrace, uuid, filesystem, lambda2, lexical_cast.
- Files: `xmake.lua` (line 34)
- Impact: Increases build time and binary size. Unused boost libraries are compiled and linked unnecessarily.
- Fix approach: Replace `boost[all]` with specific sub-packages: `boost[json,process,stacktrace,uuid,filesystem,lambda2,lexical_cast]`.

## Known Bugs

No known bugs were discovered in the current codebase state. No bug-related comments or issue trackers were detected.

## Security Considerations

**Command injection via ffmpeg path arguments:**
- Risk: The ffmpeg binary path is resolved from `--ffmpeg-path` (validated as a directory) and stored in `ctx.toolchain.ffmpegPath`. This path is then used directly in `EncodeConfig::buildCMD()` via string concatenation: `cmd += std::string{ffmpegPath.value().string()}`. While `--ffmpeg-path` is validated as a directory, the executable name could theoretically contain spaces or special characters.
- Files: `src/video/encode_config.h` (line 82), `src/picture/picture_compress.cpp` (lines 113-119)
- Current mitigation: Input and output file paths are quoted in the command string. The `--ffmpeg-path` is validated to be a directory.
- Recommendations: Quote the ffmpeg binary path itself in the command string. Consider using the boost::process argument-vector API (`bp::child(program, args...)`) instead of building a single shell command string.

**User-input paths used in external process arguments:**
- Risk: File paths from CLI arguments (`--input`, `--inputs`, `--output`, `--state-file`) are passed directly into ffmpeg/ffprobe command strings after `fs::absolute()` normalization. While the paths are quoted in the command string, a file named with backticks or shell metacharacters could theoretically cause injection if the boost::process library interprets the command through a shell.
- Files: `src/video/encode_config.h` (lines 88, 102, 105), `src/video/video_info.cpp` (lines 278-283), `src/utils/utils.cpp` (lines 324, 337, 345, 357)
- Current mitigation: Paths are quoted. `fs::absolute()` provides normalization. `boost::process::child` on Windows does not invoke a shell. On POSIX, the single-string constructor may invoke a shell depending on the boost version.
- Recommendations: Move to the argument-vector form of `bp::child(executable, vector<string> args)` on all platforms. Alternatively, explicitly use `bp::search_path` and pass arguments as a vector to avoid shell interpretation.

**Temporary files may not be cleaned up on forced termination:**
- Risk: Progress files are created in `%TEMP%/progress_{uuid}.txt`. Image compression creates temp directories at `outputDir/.compress_tmp`. If the process is killed (not Ctrl+C), these temporary files and directories may persist.
- Files: `src/video/video_encode_runner.cpp` (lines 66-67), `src/picture/picture_process.cpp` (lines 363-364)
- Current mitigation: `encodeVideo()` cleans up progress files for the no-progress path (line 262). `executeCompressPackWorkflow()` always removes the temp dir (line 460). Standard encoding progress files are cleaned by `prepareEncodeExecution()` (line 82).
- Recommendations: Consider a startup cleanup routine that removes stale temp files (check for files matching `progress_*.txt` in temp dir older than a threshold). For the temp dir removal, ensure the cleanup is in a scope guard so it runs on all exit paths.

**No input validation for --state-file path traversal:**
- Risk: The `--state-file` parameter accepts any path. No validation prevents writing job state to arbitrary filesystem locations (e.g., overwriting critical files).
- Files: `src/cmd/config_builder.cpp` (lines 318-321)
- Current mitigation: None specific to state-file. The path is normalized via `fs::absolute()`.
- Recommendations: Restrict state file writing to directories that exist and are writable. Consider a `--state-dir` parameter instead of `--state-file`, with the filename auto-generated.

## Performance Bottlenecks

**Sequential video info probing for HEVC detection in mp4 mode:**
- Problem: `finalizeVideoList()` in `video_info.cpp` probes each video file with ffprobe to check if it is already HEVC-encoded. While it uses parallel task execution, all videos must be probed before encoding begins.
- Files: `src/video/video_info.cpp` (lines 183-233)
- Cause: The `taskexec::runTasks()` call blocks until all probe tasks complete. For directories with thousands of videos, this creates a noticeable latency before the first encode starts.
- Improvement path: Stream results and begin encoding candidates as soon as they are confirmed non-HEVC. Use a producer-consumer pattern instead of collect-then-process.

**Single-threaded ffprobe calls for video info cache prewarming:**
- Problem: `prewarmWebpVideoInfoCache()` limits prewarming to `workerCount + 1` videos even though many more could be probed in parallel. The parallel task executor is used but only for the first batch.
- Files: `src/video/video_info.cpp` (lines 248-249)
- Cause: Intentional design trade-off to avoid overwhelming ffprobe processes on disk I/O. However, the cap is based on `maxParallelJobs` which was configured for encoding, not probing.
- Improvement path: Use a separate, higher concurrency for probing tasks that are read-only and lightweight.

**Image compression concurrency capped by input file size:**
- Problem: `capConcurrencyByFileSize()` reduces parallelism based on the largest single input file, even when most files are small. A single 25MB image among 1000 small images caps concurrency to 1.
- Files: `src/picture/picture_compress.cpp` (lines 179-187)
- Cause: Conservative approach to avoid memory pressure from large image decoding.
- Improvement path: Sort tasks by file size and process large files first at lower concurrency, then increase parallelism for the remaining small files. Use size-based bucketing.

## Fragile Areas

**EncodingState dual representation for progress:**
- Files: `src/core/app_context.h` (lines 79-80)
- Why fragile: `EncodingState` has `std::optional<float> lastProgress` (mutex-guarded) AND `std::atomic<float> lastProgressAtomic`. Two separate fields tracking the same concept, with different access patterns. This creates confusion about which to read under which conditions and risks reporting stale or inconsistent values.
- Safe modification: Choose one representation and remove the other. If atomic reads suffice, use only `std::atomic<float>`. If structured access is needed under mutex, drop the atomic variant. Add a comment explaining the access pattern.
- Test coverage: Encoding state tests exist in `tests/infra/progress_tests.cpp` and `tests/task_executor_tests.cpp`, but the dual-field behavior may not be explicitly tested.

**job_state::Store raw pointer in PackRequest:**
- Files: `src/pack/pack.h` (line 88), `src/pack/pack_service.cpp`
- Why fragile: `PackRequest::jobState` is a raw `jobstate::Store*` pointer. The lifetime is managed externally (via `RuntimeContext::jobState` shared_ptr). If the shared_ptr is reset before packing completes, a use-after-free occurs.
- Safe modification: Document the lifetime contract explicitly. Consider accepting `std::shared_ptr<jobstate::Store>` or `std::weak_ptr<jobstate::Store>` to make the shared ownership explicit.
- Test coverage: Pack service mock tests in `tests/pack_service_mock_tests.cpp`, but the null-pointer case is the only one tested (jobState can be nullptr).

**EncodeConfig::buildOutputPath throws instead of returning Result:**
- Files: `src/video/encode_config.h` (lines 69-79)
- Why fragile: While the overall error handling pattern uses `eh::Result<T>`, `buildOutputPath()` and `buildOutputFileName()` throw `std::runtime_error` for missing input paths. This violates the project's own convention ("All operational failures return this. Exceptions only for catastrophic errors" per AGENTS.md).
- Safe modification: Change `buildOutputPath()` and `buildOutputFileName()` to return `eh::Result<fs::path>` and `eh::Result<std::string>` respectively. Update callers to propagate errors.
- Test coverage: The encode_config tests in `tests/video/encode_config_tests.cpp` may test this validation path.

**Config validation interleaved with error construction in cmd.cpp:**
- Files: `src/cmd/cmd.cpp` (lines 628-823), `src/cmd/config_builder.cpp`
- Why fragile: Flag registration and result population are co-located in a single function (`commandLineInit`, 197 lines). The lambda-based applyMap pattern makes it hard to trace which flag maps to which `CmdParseResult` field.
- Safe modification: Keep the data-driven flag registration (it is clean). Move the applyMap construction into a separate, well-named function.
- Test coverage: CLI parsing is tested in `tests/cmd_cmd_tests.cpp` and `tests/cmd_config_builder_tests.cpp`.

## Scaling Limits

**In-memory task result storage:**
- Current capacity: `taskexec::runTasks()` stores results as a `std::vector<eh::Result<void>>` sized to the full task count. For large batches (10,000+ videos), this represents 10,000+ allocated Result objects before any processing begins.
- Limit: Memory usage scales linearly with input count. A batch of 100,000 videos would allocate ~800KB for the results vector alone, plus encoding state for each slot.
- Scaling path: For very large batches, consider streaming results or using a fixed-size result buffer.

**immer maps for encode results:**
- Current capacity: `EncodeResultsMap` is `immer::map<fs::path, bool>`, a persistent data structure. Each `set()` operation creates a new version. While efficient for small-to-medium maps, the copy overhead grows with map size.
- Limit: For >10,000 entries, the structural sharing benefit may be offset by increased memory usage from version chains.
- Scaling path: For large batches, batch updates or use a concurrent hash map instead of immutable versions.

**Default parallelism of 10:**
- Current capacity: Hard-coded default of 10 parallel jobs.
- Files: `src/video/video_batch_execution.cpp` (line 291), `src/cmd/cmd.cpp` (line 551)
- Limit: Systems with 32+ cores cannot fully utilize hardware without explicit `--jobs` override.
- Scaling path: Default to `std::thread::hardware_concurrency()` instead of a fixed 10. The codebase already does this in `video_info.cpp` (line 192).

## Dependencies at Risk

**boost[all] -- deprecated sub-modules may be pulled:**
- Risk: `boost[all]` installs the entire boost suite (200+ libraries). Some boost libraries have been deprecated (e.g., boost::signals replaced by boost::signals2). The project only uses a subset.
- Impact: Increases build time, binary size, and risk of using deprecated or unmaintained boost components.
- Migration plan: Replace with specific sub-packages as noted in Tech Debt section.

**thread-pool (BS::thread_pool) -- single-header dependency:**
- Risk: The `thread-pool` xmake package (`add_requires("thread-pool")`) may not be actively maintained. The project's own `taskexec::runTasks()` in `src/core/task_executor.cpp` uses `parallel::runIndexedTasks()` rather than the external thread-pool library directly.
- Impact: If the thread-pool package is removed or changes API, the build breaks.
- Migration plan: Verify that `parallel::runIndexedTasks()` in `src/core/parallel.cpp` does not depend on the external thread-pool package. If it does, migrate to `std::async` or keep the dependency pinned to a specific version.

**libzippp -- platform-specific toolchain requirement:**
- Risk: On Windows, libzippp requires `toolchains=clang-cl` and its libzip dependency requires `toolchains=clang`. This hard-codes the build toolchain in the dependency configuration.
- Files: `xmake.lua` (lines 42-43)
- Impact: Changing the toolchain (e.g., to MSVC) requires updating both the project toolchain and the libzippp/liblzip toolchain configurations. The POSIX path at line 45 has no toolchain constraint.
- Migration plan: Document the toolchain requirement. Consider making the toolchain configurable via xmake options rather than hard-coded.

**indicators -- progress bar library:**
- Risk: The `indicators` library (terminal progress bars) is used heavily for the compact and full-progress modes. The API is accessed via a custom `progress::ProgressContext` wrapper.
- Files: `src/core/progress.cpp`, `src/core/progress.h`
- Impact: The wrapper provides insulation but the dependency is critical to the user experience. If `indicators` is unmaintained or incompatible with future C++ standards, the progress subsystem would need significant rework.
- Migration plan: The `progress::ProgressContext` wrapper already provides an abstraction layer. Consider extending it to support alternative backends.

## Missing Critical Features

**File path tracing / dry-run mode:**
- Problem: The --dry-run feature was implemented and then fully reverted (per tech debt note). Users cannot preview what operations would be performed before running them.
- Blocks: Users must run the tool and check output to understand what will happen. The revert leaves no preview capability.

**Batch file size estimation before encoding:**
- Problem: Users cannot estimate total output size or processing time before starting a batch. The tool only reports scan count then proceeds to encoding.
- Blocks: Capacity planning. Users with large video libraries cannot determine if they have enough disk space or time.

## Test Coverage Gaps

**Error handling path coverage:**
- What's not tested: Many error paths return `eh::makeError()` but the test files focus on happy-path scenarios. The `Result` propagation through nested calls may not be tested for all error conditions.
- Files: All source files with `eh::makeError()` calls in `src/`
- Risk: Error messages may be malformed (missing format arguments), or error states may not be properly propagated through the call chain.
- Priority: Medium

**Windows-specific exec2Impl path coverage:**
- What's not tested: The `exec2Impl` function has a completely different code path for Windows (`PeekNamedPipe`, `CloseHandle`) vs POSIX (pipe streams, threads). The test suite appears to test only the running platform's path.
- Files: `src/utils/utils.cpp` (lines 42-168 for Windows, 170-277 for POSIX)
- Risk: The non-active platform path may contain compilation or logic errors that are only discovered when building on that platform.
- Priority: Low (cross-compilation CI would catch compilation errors)

**Platform-specific crash handler coverage:**
- What's not tested: The crash handler has `#if defined(_WIN32)` and `#else` paths with different signal handling. The `unhandledExceptionFilter` on Windows and `signalHandler` on POSIX may not both be tested.
- Files: `src/infra/crash_runtime.cpp`, `tests/infra/crash_runtime_tests.cpp`
- Risk: Platform-specific crash behavior may diverge.
- Priority: Low

**Cross-platform terminal color detection:**
- What's not tested: `terminal.cpp` handles ANSI escape codes and color mode detection. The `--color auto|always|never` flag behavior may only be tested on the development platform.
- Files: `src/infra/terminal.cpp`, `tests/infra/terminal_tests.cpp`
- Risk: Color output may not work correctly on terminals that don't support ANSI sequences (Windows legacy console).
- Priority: Low

---

*Concerns audit: 2026-05-22*
