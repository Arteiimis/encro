# Codebase Concerns

**Analysis Date:** 2026-04-28

## Tech Debt

### Implicit `.compact` Default in Compress-Picture Pack Path
- Issue: The compress-picture path in `picture_process.cpp:474-482` constructs a `PackPlan` without explicitly setting `.compact`. It relies on the struct default `bool compact = true` in `pack_service.h:48`. While functionally correct (compact mode IS the desired default), if someone changes the struct default, this code path would silently change behavior. The `buildPicturePackPlan` function at `picture_process.cpp:606-616` DOES explicitly set `.compact = true`, making this the lone remaining implicit default.
- Files: `src/picture/picture_process.cpp` line 474, `src/pack/pack_service.h` line 48
- Impact: Silent regression risk if `PackPlan::compact` default changes. Low risk today, but inconsistent with all other PackPlan construction sites.
- Fix approach: Add `.compact = true` to the designated initializer at `picture_process.cpp:474`. One-line addition.

### Duplicate Test Case in pack_service_tests.cpp
- Issue: The test case `"selectPackPlanIndexes preserves compact from source plan"` appears at `tests/pack_service_tests.cpp:98`. The v1.0 milestone audit flagged a duplicate at lines 131-168. Verify whether the duplicate still exists.
- Files: `tests/pack_service_tests.cpp`
- Impact: Redundant test execution, minor maintenance burden. No behavioral impact.
- Fix approach: Grep for all occurrences of `"selectPackPlanIndexes preserves compact"` in the test file. If duplicate found, remove the later copy and keep the one with the most comprehensive assertions.

### Pre-Commit Hook Hardcodes Machine-Specific Path
- Issue: The pre-commit hook at `.githooks/pre-commit:4` hardcodes `D:/clangformat/.clang-format` — an absolute Windows path on a specific developer's machine. Fails on any other machine, including CI, Linux, macOS, or other Windows users.
- Files: `.githooks/pre-commit` line 4
- Impact: Pre-commit formatting hook is non-functional for all developers except the original author. No automated formatting enforcement in practice.
- Fix approach: Either add a `.clang-format` file to the repository root and reference it relatively, or make the path configurable via environment variable with a fallback.

### No `.clang-format` Config in Repository
- Issue: The pre-commit hook references an external clang-format style file, but no `.clang-format` exists in the repository itself. Without this file, developers using editors with auto-formatting may produce inconsistent formatting.
- Files: Repository root (missing `.clang-format`)
- Impact: Inconsistent code formatting across contributors. CI cannot enforce formatting.
- Fix approach: Add a `.clang-format` file to the repository root matching the project's style, then update the pre-commit hook to reference it with a relative path.

### Large Files Deferred for Future Splitting
- Issue: The CODE_REUSE_OPTIMIZATION_PLAN Phase 6 explicitly deferred splitting `video_batch_execution.cpp` (~700 lines), `packer.cpp` (~727 lines), and `video_process.cpp` (~484 lines), concluding each "is currently centered on one workflow concern." However, `job_state.cpp` (~570 lines) was split into `job_state.cpp` + `job_state_store.cpp` + `job_state_detail.h`. These remaining large files are acknowledged technical debt.
- Files: `src/video/video_batch_execution.cpp`, `src/pack/packer.cpp`, `src/core/job_state.cpp`, `src/video/video_process.cpp`
- Impact: Large single files are harder to navigate and review. Growth pressure increases as new features are added.
- Fix approach: Monitor file growth. When a file exceeds 800 lines or begins mixing additional responsibilities, revisit the Phase 6 split plan.

### No `.env` Protection in `.gitignore`
- Issue: The `.gitignore` file does not include patterns for `.env`, `.env.*`, or credential files. If such files were ever created locally, they could be accidentally committed.
- Files: `.gitignore`
- Impact: Risk of accidentally committing environment files with secrets. Currently low since the project uses `xmake.lua` for build config, not `.env` files.
- Fix approach: Add `.env*` and `*.secret` patterns to `.gitignore` as a defensive measure.

## Security Considerations

### Command Injection via Unsanitized File Paths
- Issue: `encode_config.h:buildCMD()` at lines 81-110 constructs ffmpeg shell commands using `std::format(" -i \"{}\"", inputPath->string())`. File paths are wrapped in double quotes but special characters within paths (notably embedded double-quote `"` characters) are not escaped. On Windows, `bp::child(cmd.data(), ...)` in `utils.cpp:46` invokes the command interpreter (cmd.exe), which processes quotes and metacharacters. A specially crafted filename like `video" && malicious.exe && echo "test.mp4` could escape the quoting and execute arbitrary commands.
- Files: `src/video/encode_config.h` line 88, `src/video/video_info.cpp` line 294, `src/utils/utils.cpp` lines 46-47
- Current mitigation: Input paths come from local filesystem scanning (`media_scanner.cpp`) or CLI arguments, reducing but not eliminating the risk. Users control directory contents and CLI paths.
- Recommendations:
  1. Use `boost::process::child` with argument vector (e.g., `bp::child(ffmpegPath, args...)`) instead of shell command strings, which avoids the shell entirely.
  2. Alternatively, sanitize file paths by rejecting or escaping characters meaningful to the target shell (`"`, `&`, `|`, `;`, `%`, `^` on Windows).
  3. Validate input paths against a whitelist of allowed characters before passing to shell commands.

### Unvalidated External Binary Resolution
- Issue: `findFFprobe()` at `utils.cpp:317` and `findFFmpeg()` at `utils.cpp:338` search PATH or a user-supplied install directory for the `ffprobe`/`ffmpeg` executables. If PATH is compromised (e.g., a malicious directory prepended), a trojaned binary could be executed with the application's privileges.
- Files: `src/utils/utils.cpp` lines 315-355, `src/infra/toolchain.cpp` lines 11-19
- Current mitigation: User specifies `--ffmpeg-path` for explicit control. System PATH fallback is a convenience feature.
- Recommendations:
  1. Log the resolved binary path at startup (already done at `toolchain.cpp:25-26`).
  2. Optionally validate the resolved binary via version string check or checksum.
  3. Consider warning when falling back to PATH-discovered binaries.

### Error Information Leakage in Output
- Issue: Error messages include full file paths (`encode_config.h:26`), command strings (`utils.cpp:40`), and internal state details. While useful for debugging, this may leak filesystem structure information to log consumers.
- Files: `src/video/encode_config.h` line 26, `src/utils/utils.cpp` line 40, `src/video/video_batch_execution.cpp` lines 343-345
- Risk: Low — CLI tool for local use, not a network service. Logs are local.
- Recommendations: Ensure verbose logging (`--verbose-echo`) is opt-in and off by default. Maintain current practice of using `spdlog::debug` for sensitive details and `spdlog::info` for user-facing messages.

## Performance Bottlenecks

### Monitor Poll Loop Still at 50 Hz
- Issue: The encoding progress monitor loop in `video_batch_execution.cpp:512` polls at `sleep_for(20ms)` (50 Hz). The VIDEO_ENCODE_PERF_OPTIMIZATION_PLAN Phase 3 recommended changing to 100ms (10 Hz) for a 5× reduction in CPU usage with no visible impact on terminal rendering. This phase was planned but never implemented.
- Files: `src/video/video_batch_execution.cpp` line 512
- Cause: The 20ms interval was the original design. Phase 3 (adjust monitor poll interval) of the performance optimization plan was documented but skipped.
- Improvement path: Change `sleep_for(20ms)` to `sleep_for(100ms)` at line 512. Optionally implement adaptive polling (50ms for first 5s, 200ms thereafter).

### Unimplemented Performance Optimization Phases
- Issue: The VIDEO_ENCODE_PERF_OPTIMIZATION_PLAN documents 7 phases; only Phases 1-2 (totalFrames caching, lastProgressAtomic) were implemented. Phases 3-7 remain as planned but unimplemented:
  - **Phase 3**: Monitor poll interval adjustment (see above)
  - **Phase 4**: `finalizeState()` critical section merging — partially done (currently single lock at `video_batch_execution.cpp:303-322`)
  - **Phase 5**: WebP starting quality heuristic — `encodeWebpWithTargetSize()` at `video_encode_runner.cpp:173` always starts at quality 80 regardless of input size
  - **Phase 6**: Single-owner `progressFilePath` — `prepareEncodeExecution()` at `video_encode_runner.cpp:64-67` has redundant fallback path
  - **Phase 7**: Sequential path `immer::map` → `std::unordered_map` — `runEncodingWithoutProgress()` still uses persistent tree allocations
- Files: `src/video/video_batch_execution.cpp`, `src/video/video_encode_runner.cpp`
- Impact: Unnecessary CPU usage in monitor loop, extra ffmpeg passes for large WebP encodes, minor allocation overhead. Not blocking functionality.
- Fix approach: Execute remaining phases in order. Phase 3 is lowest risk (one-line change). Phase 5 provides the biggest perf win for WebP users.

### `immer::map` in Sequential Insertion Path
- Issue: `runEncodingWithoutProgress()` in `video_batch_execution.cpp` uses `vidsRunRes.set(vidPath, success)` in a tight loop. Each insertion creates a new persistent tree node. The plan Phase 7 proposed replacing with `std::unordered_map` for the loop and converting to `immer::map` at the end.
- Files: `src/video/video_batch_execution.cpp`
- Cause: The `EncodeResultsMap` type is `immer::map<fs::path, bool>` optimized for concurrent read access, but the sequential path doesn't need this.
- Improvement path: Switch to temporary `std::unordered_map` during the sequential loop, convert at return boundary.

## Fragile Areas

### Compress-Picture Pack Path (Implicit Default)
- Files: `src/picture/picture_process.cpp` lines 380-484
- Why fragile: The `PackPlan` at line 474 omits `.compact`, relying on struct default. If `PackPlan::compact` default changes from `true` to `false`, compact progress mode silently breaks for the compress-picture workflow. All other PackPlan construction sites (video process at `video_process.cpp:434`, picture pack-only at `picture_process.cpp:615`, directory pack at `packer.cpp:820`, selectPackPlanIndexes at `pack_service.cpp:160`) explicitly set `.compact`.
- Safe modification: Add `.compact = true` to line 474's designated initializer. One-line, zero-risk change.
- Test coverage: Unknown — the compress-picture path may have limited direct test coverage for progress bar behavior.

### Shell Command Construction in `EncodeConfig::buildCMD()`
- Files: `src/video/encode_config.h` lines 81-110
- Why fragile: The function concatenates a shell command string with unsanitized path components. Paths containing spaces, quotes, or special characters could produce invalid commands. The double-quote wrapping provides partial protection but is not comprehensive. Additionally, Windows paths use backslashes, which are escape characters within double quotes in cmd.exe.
- Safe modification: Convert to argument-vector invocation (`bp::child` with argv) or add proper shell escaping.
- Test coverage: `tests/video/encode_config_tests.cpp` exists and tests `buildCMD()` output format.

### E2E Test Build Configuration
- Files: `tests/e2e/e2e_test_utils.h`, `xmake.lua` lines 52-57, 78-92
- Why fragile: The VIDEO_ENCODE_PERF_OPTIMIZATION_PLAN noted a "pre-existing build error (missing `infra/stop_signal.h` in e2e `test_utils.h` include path)" on 2026-04-24. The e2e test target at `xmake.lua:78-92` uses `add_deps("encro", "encro_e2e_tool")` which depends on the main binary target and the fake media tool. Configuration churn between these targets may cause build breaks.
- Safe modification: Verify e2e tests build and pass with `xmake build e2e_tests && xmake run e2e_tests`. Fix include paths if broken.
- Test coverage: E2E tests cover end-to-end CLI workflows — the highest-value tests for catching integration regressions.

## Scaling Limits

### Archive Group Size Constants
- Issue: Archive/zip group size limits are defined as constants (e.g., `kDefaultMaxArchiveGroupSize`, `kMaxPicturesPerPack = 2000` at `picture_process.cpp:440`) with hardcoded values. For extremely large directories with thousands of files, these create many small archive groups.
- Files: `src/picture/picture_process.cpp` line 440-446, `src/pack/pack_service.cpp`
- Current capacity: Group size policies handle typical use cases. No known failure reports.
- Limit: Very large directories (10,000+ files) may produce many archive files, though this is by design.
- Scaling path: Consider making group size limits configurable via CLI flags for power users.

### Single-Threaded Orchestration Dispatch
- Issue: The main `app_entry.cpp` → `pipeline.cpp` dispatch path processes one workflow at a time (video, picture, or pack-only). Multi-directory batch processing is not supported in a single invocation.
- Files: `src/app/pipeline.cpp`, `src/app/app_entry.cpp`
- Limit: One `--input-path` per invocation. Users with multiple directories must run encro multiple times.
- Scaling path: Support multiple `--input-path` values with parallel or sequential processing per directory.

## Dependencies at Risk

### Boost Process `v1` Namespace
- Issue: `utils.cpp:32` explicitly uses `namespace bp = boost::process::v1`. If Boost ever releases `boost::process::v2` and deprecates v1 (as happened with boost::asio), this will require migration.
- Files: `src/utils/utils.cpp` line 32
- Impact: Low — Boost Process is stable and widely used. No deprecation announced.
- Migration plan: Monitor Boost release notes. If v2 ships, evaluate migration cost (likely minimal — the API surface used is small).

### External Formatting Tool Dependency
- Issue: The pre-commit hook requires `clang-format` to be on PATH. No version pinning or fallback.
- Files: `.githooks/pre-commit` lines 6-9
- Impact: Different clang-format versions may produce different formatting, causing churn. Missing clang-format silently blocks commits.
- Migration plan: Either document the required clang-format version, or add a `.clang-format` file with version requirements, or use a project-local clang-format installation via package manager.

## Missing Critical Features

### No Structured Logging Output
- Issue: All logging goes to `spdlog` sinks (console/file). There is no machine-parseable log format (JSON, structured key-value) for monitoring or CI integration.
- Blocks: Automated performance monitoring, CI log analysis, programmatic progress extraction (beyond terminal output parsing).

### No Installer or Package Distribution
- Issue: The xmake xpack configuration (`xmake.lua:94-103`) defines packaging targets including `nsis`, `zip`, `tarxz`, but there is no CI pipeline to build and publish these packages. Users must build from source.
- Files: `xmake.lua` lines 94-103
- Blocks: Easy distribution to non-developer users. One-click installation.

## Test Coverage Gaps

### Compress-Picture Path Coverage Uncertain
- What's not tested: The compress-picture workflow path in `picture_process.cpp` (lines ~380-489) — specifically the PackPlan construction without explicit `.compact`. Unit tests focus on `picture_compress.cpp` and `picture_process.cpp` entry points, but the internal PackPlan construction may not have dedicated assertion coverage for the `.compact` field.
- Files: `src/picture/picture_process.cpp`, `tests/picture/picture_process_tests.cpp`
- Risk: Silent `.compact` regression if `PackPlan` struct default changes.
- Priority: Low

### E2E Tests Potentially Broken
- What's not tested: End-to-end CLI workflows if the e2e test target fails to build (reported pre-existing build error as of 2026-04-24). The e2e tests cover: default encoding+packing, full-progress mode, pack-only, picture mode. If these can't build, the highest-value integration tests are unavailable.
- Files: `tests/e2e/encro_e2e_tests.cpp`, `xmake.lua` target `e2e_tests`
- Risk: Integration regressions go undetected until manual testing.
- Priority: High

### No Stress/Load Testing
- What's not tested: Large-scale batch encoding (100+ files), large archive groups, concurrent worker saturation, memory usage under load.
- Files: No load test files exist
- Risk: Performance degradation or resource exhaustion at scale goes undetected.
- Priority: Medium

---

*Concerns audit: 2026-04-28*
