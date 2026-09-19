# Backlog

Deferred issues, recorded with diagnosis so future fixes can skip the
investigation. Newest last.

## RESOLVED: Flaky `test-parallel` hang (unit shard stuck before first test)

- **Status:** fixed in c5977dc (`crash-veh-loader-lock-safety`: DLL-load zone
  gating the crash paths) · **Found:** 2026-09-08 (during console-message-conventions verification)
- **Symptom:** `xmake test-parallel` sometimes hangs indefinitely with one unit
  shard's `tests.exe` stuck right after the Catch2 seed line (0 tests run,
  near-zero CPU); the same binary passes all 12 shards in ~13 s on other runs.
  Reproduces deterministically for a given `--rng-seed` when the affected test
  order is replayed.
- **Root cause (cdb stack capture):** `onnxruntime_providers_cuda` raises an
  exception during its DLL static init (`ucrtbase!initterm`, provider
  discovery). The VEH crash handler installed by
  `crash::installHandlers()` (hardening-crash-diagnostics) treats it as fatal
  and calls `writeCrashReport` → `captureStacktrace`, which initializes the
  dbgeng COM service. Under loader lock that COM init deadlocks
  (`dbgeng!DebugCreateEx` → `SleepEx` forever), so the process never exits and
  the shard never finishes.
- **Root cause confirmed 2026-09-08** (source walk + live repro; supersedes
  the "dbghelp" assumption — `xmake.lua`'s `add_syslinks("dbghelp")` is a red
  herring, the import table carries no dbghelp.dll):
  - MSVC STL `std::stacktrace` (stl/src/stacktrace.cpp) symbolizes via
    dbgeng, not dbghelp: lazy `LoadLibraryExW(dbgeng.dll)` → `DebugCreate` →
    `AttachProcess(self, NONINVASIVE)` → `WaitForEvent(0, INFINITE)` — an
    indefinite debug-event wait that cannot complete from a thread holding
    the loader lock mid-DLL-init. Matches the cdb frames exactly.
  - Trigger chain: `tests.exe` imports `onnxruntime.dll`; with model files
    present (`~/.encro/models`), `real_model_tests` constructs `OnnxTagger` →
    `ensureGpuRuntimePaths` `LoadLibraryExW`s `onnxruntime_providers_cuda.dll`
    (onnx_tagger.cpp); its static init raises a first-chance AV (a fatal code
    in `isFatalExceptionCode`), so `vehFatalHandler` runs the full report on
    the loader-lock-holding thread. `captureStacktrace` is the first statement
    of `writeCrashReport`, hence zero output before the hang.
  - Seed determinism mechanism: the dbgeng engine is a per-process phoenix
    singleton behind an SRW lock — the *first* `captureStacktrace` in a
    process performs the deadlocking attach; later calls only query. A shard
    hangs iff its first test is the onnx load *and* no earlier test warmed
    the symbolizer (e.g. crash_runtime tests do); shard composition is fixed
    by `--rng-seed`.
  - Live repro (twice, different seeds): `tests.exe "[real-model]"` alone
    prints only `Filters:` + seed line and never finishes; main thread
    `WaitReason=ExecutionDelay` (SleepEx family), CPU frozen across samples
    (0.3 s → 0.3 s).
  - Design blind spot: hardening-crash-diagnostics design D1's risk list only
    costed "one integer comparison per first-chance exception" and never
    considered that first-chance handling runs the report path on threads
    that may hold the loader lock, where LoadLibrary/COM/debug-engine waits
    are forbidden. Same latent deadlock exists in `encro.exe` (it runs the
    same provider preload), not just tests.
- **Fix direction:** the VEH handler must not hijack exceptions that originate
  inside third-party DLL initialization (or at minimum must not run the
  dbgeng-backed stack capture on the loader-lock thread); consider
  first-chance/continuable filtering and a hard timeout fallback to
  `MiniDumpWriteDump` or stderr-only reporting.
- **Impact:** CI/verification reliability only; product code unaffected.

## RESOLVED: e2e `encro webp CLI can use the fake ffmpeg toolchain`

- **Status:** fixed in 1a4550d (exec2 unquoted spaced-path resolution) · **Found:**
  2026-09-08 (during console-message-conventions verification)
- **Root cause (not a race):** `5cec2fa` added the exit-127 token check to
  exec2. On Windows `quoteToolPath` emits bare paths, and the shell parse
  splits a spaced path at its first space; the new `fs::exists(argv[0])` then
  rejected the truncated token before the launcher's whitespace-extension
  search could resolve the real tool. Deterministic on Windows for spaced
  `--ffmpeg-path` roots; CI (posix, quoted paths) stayed green, which read as
  "flaky". exec2 now mirrors the launcher by accepting the first argv-prefix
  join that names an existing file (regression test:
  "exec2 resolves an unquoted tool path containing spaces").

## RESOLVED: narration tests capture log lines on stdout (order-dependent)

- **Status:** fixed in `test: keep unit-test log fallback off stdout` ·
  **Found:** 2026-09-08 (CI run 34230413061, debug job; release/coverage green)
- **Symptom:** `video scan narration prints one outcome line on non-TTY output`
  (`tests/video/video_process_orchestration_tests.cpp:307`) intermittently
  fails with `captured.find("Scanning") != npos` (plus `candidate` at `:308`),
  while re-running the same commit goes green.
- **Root cause:** Catch2 v3.15 defaults to `--order rand` with a fresh seed per
  run, so test order varies. The unit-test binary never calls
  `logging::setup()`, and `LOG_*` falls back to
  `spdlog::default_logger_raw()` (`src/logging/logging.h`), which is spdlog's
  built-in **stdout** logger. Whenever the narration test ran before the first
  test that calls `logging::shutdown()` (which resets the default logger to
  null), `LOG_INFO("Scanning input path: ...")` and
  `LOG_INFO("Scan completed: N candidate video(s)")`
  (`src/video/video_process.cpp:176,185`) landed in the test's `StdoutCapture`
  file and tripped the absence assertions. Reproduced locally: the test alone
  fails; paired with `logging::shutdown: cleans up spdlog global state` it
  fails only when the narration test runs first. ~2% per CI job — the test must
  precede the run's first `logging::shutdown()`. Same mechanism applied to
  `packer_tests.cpp:61` (`Scanning` from `src/pack/packer.cpp:756`).
- **Fix:** sink-less default logger installed in `tests/test_main.cpp`;
  regression guard is the `[test-utils][meta]` child probe in
  `tests/infra/crash_runtime_tests.cpp`. The crash-on-demand child keeps the
  stdout logger because its report is what the parent test reads.
- **Impact:** CI reliability only; product code unaffected.

## Flaky release-job SIGSEGV in the unit suite (heap corruption, victim test unrelated)

- **Status:** open — no repro; rerunning the same commit's failed job passed
  (run 34768754276 rerun) and the next push run was green (34771731216) ·
  **Found:** 2026-09-13 (CI push of da34fd5, `unify-progress-scroll`)
- **Symptom:** the `release` job's unit step exits 139. `/tmp/ut.log` ends with
  `[CRASH] fatal signal 11` plus a symbol-less stack guarded by `#00 0x... in
  libc.so.6`, and the JUnit report attaches `SIGSEGV - Segmentation violation
  signal` to `known options carry completion metadata`
  (`tests/cmd_completion_capture_tests.cpp:46`, last test of the run).
- **Diagnosis:** that test only does registry lookups, temporary-string
  compares and `malloc`/`free`; with the fault inside libc called from the test
  binary it is the first allocation after heap corruption — the victim, not the
  culprit. The capturing change (the `ProgressContext` repaint clock) never
  touches the heap off a TTY: `progressBarsAllowed()` gates every render and
  the ticker only bumps `tickCount_` under the mutex, so CI (no TTY) runs it
  inert. The crashing file is unchanged since 2026-09-03. Only release is
  affected — debug and coverage pass on the same commit — which points at a
  release-only (LTO, no `_MSVC_STL_HARDENING`) silent out-of-bounds/UAF write
  earlier in the suite; organize's ONNX Runtime inference and the pack cancel
  paths are the standing candidates.
- **Next step:** release carries no DWARF, so the captured stack cannot be
  symbolized. Rebuild with `xmake f -m releasedbg` (ASan) and run the suite
  repeatedly before re-reading the stack; a symbolized release job would make
  the next occurrence self-diagnosing.

## test-report assertion count vs the console summary (cosmetic)

`xmake test-report` falls back to the JUnit `tests=` attribute when the console
log has no `All tests passed (...) in (...) test cases` line, and the two
reporters disagree: the JUnit reporter emits one `<testcase>` per Catch2
section, so its `tests=` counts sections (850 for 713 cases at the time of
writing) and its total differs from the console assertion count (31837 vs 31827
on one and the same run). Verdicts are unaffected — the JUnit `failures=` and
`errors=` attributes agree with the console failures — and the parallel-shard
plugin takes its counts from the console summaries. Recorded so a future reader
does not chase it; the honest fix, if it ever matters, is to print the console
summary whenever it exists.

- **Deferred:** a second fixed-seed shuffled CI pass per mode. A fixed seed only
  permutes whatever order the build happened to register, so it repeats one
  permutation forever; the seed printed in `ut.log` already makes any failure
  reproducible, and every shuffled run explores new orders for free.

## Parallel-shard assert totals: resolved for membership, residue for e2e counts

`xmake test-parallel` used to let Catch2 partition the suite
(`--shard-count`/`--shard-index`, which slice the *randomised execution order*),
so shard membership changed every run, one shard could draw every heavy case
(measured 7364 assertions in one shard against 702 in another) and the printed
aggregate was not a metric: the same 770 cases reported 21113, 11350, 7375 and
17973 assertions against 16306 in a single process.

- **Resolved:** shards are now partitioned by enumerated case name (spec files,
  LPT over a recorded per-case cost model), preflight rejects an unsound
  enumeration or assignment, and each shard's executed case count must equal what
  it was assigned. The unit suite's printed aggregate now equals its
  single-process total exactly (15594 assertions in 723 test cases on three
  consecutive runs), and the case count is stable because every enumerated case
  runs exactly once.
- **Residue:** the e2e suite's assertion count is not conserved, and cannot be:
  its cases assert per observed condition, and the shard's *private* temp root
  changes those conditions. Replaying one shard's spec file with an inherited
  temp root gives 220 assertions against 231 with a private `TMP`/`TEMP`, a
  single-process run reports 719-729, and sharded runs report 769-815 for the
  same 47 cases. The per-site diff between a single-process run and the four
  spec-file replays localises the movers to per-invocation helper sites
  (`tests/test_utils.h(500)`, `tests/e2e/encro_e2e_tests.cpp(71)`).
- **Next step:** nothing, unless the e2e number ever needs to be a metric — that
  would mean making those cases assert once per case instead of once per temp-dir
  entry. The harness's own contract (coverage exact, aggregate = sum of what ran)
  holds regardless.

## Batch progress in `runTasks`: narrower than the review card claimed (deferred)

- **Status:** open — fact-checked 2026-09-19, scope reduced, deliberately not
  scheduled until `unify-task-outcome` lands.
- **Original candidate:** "move batch counting, rate, cursor and ETA into
  `runTasks`" (architecture-review card 7) — the executor knows how many tasks
  finished but offers no completion hook, so each caller re-wraps every task to
  count for itself.
- **Why the broad version does not hold:** the units are not tasks. Five sites
  count five different things with four different formulas — tasks
  (`src/organize/pipeline.cpp:150-190`, `done/total` plus an `img/s` rate),
  windows mapped onto a shared bar (`src/preview/preview_process.cpp:455-470`,
  `windowBase + (85-windowBase)*done/size`), the four step phases of one probe
  point (`src/video/encode_probe.cpp:535-574`, `kStepsPerProbePoint`), files
  inside one packer invocation (`src/pack/packer.cpp:438-449`), and encoded
  frames (`src/video/video_encoding_state.cpp:186`). The executor cannot own a
  unit it is never told.
- **`hideCursor` is not derivable from `TaskPlan::progress`:** pack passes
  `progress = nullptr` with `hideCursor = true`
  (`src/pack/pack_service.cpp:298-303`) because its bar is drawn through a
  different `ProgressContext` (`CompactStatus::initBar`). The real rule — "a bar
  is being drawn during this batch, by whichever context draws it" — is the
  caller's knowledge, so the field stays; only its rationale belongs on the
  field instead of in organize's call-site comment.
- **ETA is already owned:** `progress::ProgressContext::etaSeconds` and
  `formatEtaBadge` are the ETA home, pinned by the `progress-eta-badge` spec.
- **What survives (the narrow version):** an optional completion hook on
  `TaskPlan` — `onTaskFinished(done, total)` — so the three sites that wrap every
  task or carry their own atomic (organize's counting wrapper, preview's
  `windowsCompleted`, picture's `BatchState::completed`) read one counter from
  the executor instead; plus moving the cursor rationale onto
  `TaskPlan::hideCursor`. Narration text stays byte-identical; the saving is
  roughly 20 lines in organize and one atomic in each of preview and picture.
- **Sequencing:** same call sites and same result loops as
  `unify-task-outcome`, so do it after that change lands rather than touching the
  same nine sites twice.

## Pack's test-only surface: two of three items hold, the third is a recorded decision

- **Status:** open — fact-checked 2026-09-19; the size-default half is blocked by a
  prior decision, and the rest is deferred for sequencing.
- **What holds (test-only public surface):** the paths overload of
  `Packer::packFilesToZip` (`src/pack/packer.h:25`, definition
  `src/pack/packer.cpp:387`) has no production caller — production uses the
  entries+progress overload (`src/pack/pack_service.cpp:397`) and the
  entries+callbacks overload (`:211`), while the paths form is called only from
  `tests/packer_tests.cpp:218,260,304,347,487`. `PackRequest::entryNameForFile`
  (`src/pack/pack.h:92`) is never set in production either, so
  `applyEntryNameOverrides` (`src/pack/pack.cpp:263-275`) is dead there; its only
  producer is `tests/pack_execute_tests.cpp:521`.
- **What is blocked, not forgotten:** the 490 MB defaults in `packer.h:50,56`
  versus the 500 MB `kDefaultMaxArchiveGroupSize` (`pack_types.h:95`, used by
  `packer.h:71`). `reduce-over-engineering` recorded the rejection — "490*1024*1024
  vs 500 MB are genuinely different defaults — merging would silently change
  behavior". Reopen only if a decision says the two grouping entry points should
  share one limit.
- **What to keep:** `PackPlan::onBeforeArchiveClose` (`pack_plan_internal.h:26`).
  Production passes it through (`pack_service.cpp:216,466`) and
  `tests/pack_service_tests.cpp:190` uses it to hold a group open mid-close for the
  finalizing indicator; deleting it would force that test onto a different gate for
  no behavioral gain.
- **Next step:** delete the paths overload and the entry-name override, and
  re-point the five `packFilesToZip` cases plus the `entryNameForFile` case at the
  entries API — they cover real behavior (archiving, duplicate entry-name
  disambiguation, summary entry names, multi-entry) rather than the seam. Sequence
  after `unify-task-outcome`, whose nine call sites include this pack loop.

## Help renderer inside `cmd.cpp`

- **Status:** open — fact-checked 2026-09-19; deferred for sequencing only, the
  split itself is sound.
- The help-layout block is `src/cmd/cmd.cpp:32-530` (about 500 of the file's 1144
  lines): layout resolution, cell and description formatting, column widths, section
  and command rendering, `makeHelpFormatter`, `makeSubcommandHelpFormatter`.
  Registration starts at `registerOrganizeSubcommand` (`:532`) and the formatters are
  wired at `:569,638,660,715,1111`.
- No external caller touches the internals: everything goes through
  `CmdParseResult::helpText()` (`src/cmd/cmd.h:90`), including every assertion in
  `tests/cmd_cmd_tests.cpp:218-463`. An extraction therefore needs no test edits —
  only the wiring moves.
- **Next step:** move the block into `src/cmd/help_layout.{h,cpp}` behind
  `(app, layout) -> std::string`. Sequence after `unify-config-key-table`, which
  edits the registration half of the same file and `option_specs.h`.

## Duplicate lines in the human-readable log file

- **Status:** resolved — fixed in 9ba5187 — found 2026-09-19 while verifying
  `unify-run-teardown`; pre-existing and unrelated to that change.
- The counting sink both wraps a sink and is inserted beside it:
  `src/logging/setup.cpp:462-466` builds `LevelCountingSink(sinks.front())` and then
  inserts it at the head of the same vector, while the pass-through class (`:50-56`)
  forwards every record to `next_`. A logger fans one record out to every sink it holds,
  so the wrapped sink receives it twice — once directly, once forwarded.
- The wrapped sink is the human-readable rotating file sink (pushed first at `:396`), so
  every line appears twice in the `.log`. Console and ndjson output are unaffected, and
  `level_counts` still counts each record once.
- **Next step:** make `LevelCountingSink` count-only (drop `next_` plus the `flush` and
  `set_pattern` delegation — the vector already delivers the record to every sink).
  Replacing the head instead of inserting alongside it does not work: the head *is* the
  human-readable file sink, so that would drop the log entirely. Pin it with a case that
  logs one record against a temp log root and asserts exactly one matching line.

## Shared scratch root makes concurrent suites interfere

- **Status:** resolved — fixed in af6ee6a — observed 2026-09-19 by four worktree
  agents running their suites in parallel; environmental, not a defect of any single
  change.
- `workdirs::scratchDir()` is `fs::temp_directory_path() / "encro" / kScratchDirName`
  (`src/core/work_dirs.cpp:11-13`): machine-global, no per-process component, swept only
  after 24h (`:19-25`).
- Any "the root is empty" assertion therefore races with a sibling process's live files:
  `CHECK(leftoverProbeDirs().empty())` (`tests/video/encode_probe_tests.cpp:499,529,927,943`,
  helper at `:458`), `CHECK_FALSE(leftover)` (`tests/preview/preview_process_tests.cpp:279`)
  and the `sweepScratchDir` case (`tests/work_dirs_tests.cpp:249`, call at `:272`) each
  failed once while another `tests.exe` was mid-run and passed on rerun with no code
  change.
- **Next step:** give the scratch root a per-process component (pid or a per-run suffix)
  and keep the 24h sweep over the parent, so the emptiness assertions hold by
  construction.

## Scalar trailing returns drift from the documented convention

- **Status:** resolved — fixed in 93ff533 — pre-existing; `AGENTS.md:25` pins prefix returns
  for scalars and `void`, and the codebase has eight counter-examples in `src`
  (`cmd/completion_install.cpp:92,197`, `organize/cluster.h:57`, `organize/cache.cpp:70`,
  `organize/assign.h:75`, `organize/scan.cpp:23`, `core/sha256.cpp:30`,
  `tagger/onnx_tagger.cpp:87`) plus seven files under `tests/`.
- Effect: new code that follows the rule sits next to neighbours that do not, so a
  reviewer has to decide per file; the `segment_plan.{h,cpp}` helpers introduced by
  `extract-segment-plan` were brought into line in `203aa8a`.
- **Next step:** one mechanical sweep converting scalar `auto f(...) -> T` to prefix
  style, in its own commit (no behavior change) — or relax `AGENTS.md:25` if trailing is
  the intended house style after all.
