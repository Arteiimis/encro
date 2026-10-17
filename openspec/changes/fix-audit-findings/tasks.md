# Tasks

Each group is one commit: the failing test first (RED), the fix, then the checkbox tick. The `verify:` clause names what proves the task, not what to eyeball.

## 1. Path identity (spec `path-identity`)

- [x] 1.1 RED: add `tests/path_identity_tests.cpp` pinning that the identity string of `A.mp4` and `a.mp4` differs off Windows and matches on Windows, and that `stablePathLess` orders both pairs identically. The POSIX branch is the one that names the defect and only CI runs it; on Windows the same case guards the opposite regression, verified by removing the fold from `pathIdentityString` and watching exactly the identity and `shortPathHash` assertions fail.
- [x] 1.2 Add `collisionnaming::pathIdentityString` (exact case off Windows, folded on Windows) and move the three identity families to it — `makeEncodeTask`'s id, `buildPackEntryStableKey`, and `shortPathHash` (conflict-handled names and `segmentDirForTask`) — keeping `stablePathString` as the ordering key; verify: 1.1 passes, `tests/naming_strategy_tests.cpp` and `tests/video/segment_dir_tests.cpp` stay green.
- [x] 1.3 Verify two inputs differing only in case stay two tasks; verify: `tests/job_state_tests.cpp` case where `A.mp4` and `a.mp4` merge into two task records and neither is recovered from the other, and `tests/packer_tests.cpp` case where their pack grouping keys differ. The pack key itself (`buildPackEntryStableKey`, private) has no external surface, so the packer case observes the derived keys through the order `groupPackEntries` sorts by, and its key-distinctness assertion is POSIX-only for the same reason as 1.1.

## 2. Encoding settings in state matching (spec `job-state-resume-matching`)

- [x] 2.1 RED: add `tests/job_state_tests.cpp` cases — a state written with one set of settings is not resumed by a run with a different `crf`, codec, preset or floor, a state with identical settings resumes, and a state without recorded settings resumes; verify: the mismatch cases and the legacy case went red before 2.2 (5 mismatch pairs plus the missing-field `REQUIRE`).
- [x] 2.2 Add `crf`, `videoCodec`, `nvencPreset` and `minVmaf` to a nested optional `EncodeSettings` on `ConfigSnapshot` (absent parent = legacy match), `toJson`/`fromJsonConfig` and `configMatches`; verify: the 2.1 cases pass and `tests/cmd_config_builder_tests.cpp` stays green. `imageQuality` is deliberately out: it changes no video output's bytes, and `tests/picture/picture_video_webp_tests.cpp` pins that a quality change keeps the conversion cache reusable.
- [x] 2.3 Add a case asserting the discard warning text for a settings-caused mismatch: `tests/app/pipeline_pack_only_tests.cpp` runs the same input twice with a changed `crf`, captures stderr around the second run and asserts the "does not match" / "discarding" message plus the absent resume notice; verify: the case passed only after 2.2.

## 3. Crash-safe state replace (spec `job-state-resume-matching`)

- [x] 3.1 Extract the current commit path into `jobstate::detail::replaceStateFile(staged, state)` as a pure move — no behaviour change, the remove-then-retry stays inside it; verify: the helper is called by `flushSnapshot` and the existing `tests/job_state_tests.cpp` plus `tests/logging_error_context_tests.cpp` stay green.
- [x] 3.2 RED: add `tests/job_state_tests.cpp` cases against `replaceStateFile` — a missing staged file must fail and leave the state file untouched (the extracted remove-first retry deletes it), and a successful replace must leave no `.tmp`/`.bak` sibling; verify: the first case goes red against the 3.1 extraction.
- [x] 3.3 Implement the stage-survives-on-failure semantics (rename current to `.bak`, replace, restore `.bak` on failure, delete `.bak` on success) and drop the remove-then-retry step; verify: 3.2 passes and the suites named in 3.1 stay green. The existing failure-injection in `job state mark operations report persistence failures` moved from "make the state path a non-empty directory" (which the backup step now simply renames out of the way) to "block the `.bak` slot", so it still exercises a commit that cannot complete.

## 4. Probe cache never fatal (spec `video-encode-probing`)

- [x] 4.1 RED: add `tests/video/probe_cache_tests.cpp` case writing `{"version":1}` (no `entries`) and asserting `load()` yields an empty cache; verify: it threw `boost.json: out of range ... try_at` before the fix.
- [x] 4.2 Guard the entries read in `probecache::load` with `contains` + `is_array`; verify: 4.1 passes and the existing corrupt-JSON/version cases stay green.

## 5. Model file repair (spec `image-character-organize`)

- [x] 5.1 RED: add `tests/tagger/model_store_tests.cpp` case where a model file exists with the wrong size and the download decision still includes it; verify: it went red (0 files to fetch) while `filesToFetch` used the existence predicate.
- [x] 5.2 Drive the download loop from the shared `missingFiles` result; verify: 5.1 passes and the loopback-server download cases stay green.
- [x] 5.3 Add the idempotence case: a second `downloadMissing` run downloads nothing. Asserted at the decision level — once every file matches its pin, `filesToFetch` is empty, so the loop is a no-op — because a real `downloadMissing` run would fetch the pinned models (hundreds of MB) over the network.

## 6. Staged cuDNN extraction (spec `image-character-organize`)

- [x] 6.1 RED: add a `tests/tagger/model_store_tests.cpp` case where an extraction fails after the first DLL and asserts `cudnn64_9.dll` does not exist; verify: the failure is injected by a directory standing where the second DLL belongs (its install then cannot complete), and dropping the marker-last ordering makes the case go red — the marker is there while the extraction never finished.
- [x] 6.2 Stage each DLL as `<name>.part`, check the write, then rename the staged files in one pass with `cudnn64_9.dll` last; verify: 6.1 passes, and a failed rename removes the staged leftovers. `ensureCudnn` gained an optional archive path so both cases run against a local archive instead of downloading the pinned 557 MB one.
- [x] 6.3 Add the complete-extraction case: every DLL lands, `cudnn64_9.dll` exists, a second call skips the archive entirely (it is pointed at a path that does not exist), and no `.part` file is left behind; verify: the new case passes.

## 7. Directory-walk tolerance (spec `error-visibility`)

- [x] 7.1 RED: add `tests/pack_execute_tests.cpp` case packing a tree in which a file vanishes after the walk and assert the pack proceeds without the file; verify: it threw `std::filesystem::filesystem_error` out of the size read before 7.3 (re-confirmed by restoring the unguarded `fs::file_size` and watching the case go red).
- [x] 7.2 Add `media::listFiles(root, recursive, skippedDirNames)` next to `scanByExtensions`, both built on one `walkDirectory` holding the guarded loop and the per-failure warning; verify: `tests/media_scanner_tests.cpp` covers a missing root (error, not an empty listing), hidden files kept while a named work directory is skipped, and — off Windows — an unreadable subdirectory warned about while the rest of the tree is still listed.
- [x] 7.3 Replace the packer's `scanDirectoryFiles` with `media::listFiles` plus the excluded-path check, and read file sizes through one guarded `readableFileSize` in `groupFilesBySize`/`groupPackEntries`/`groupPackEntriesWithSubparts`; verify: 7.1 passes, and the log names the file it could not read.
- [x] 7.4 Adopt the guarded loop in the remaining flagged walks — `organize/disposition.cpp`, `organize/teach.cpp`, `picture/picture_process.cpp`'s cache sweep, and the ffmpeg/ffprobe search; each now reports a failing entry and stops that walk instead of throwing. `batch/batch.cpp` already constructed its iterator with an `error_code` and was left alone. Verify: the existing organize, picture and tool-search suites stay green, and the unreadable-entry behaviour they share is pinned by the `[media-scanner]` case from 7.2.
- [x] 7.5 Collapse `findFFprobe`/`findFFmpeg` into one `findTool(name, installDir)` with thin wrappers; verify: `tests/utils_tests.cpp` tool-search cases stay green.

## 8. Video container set (spec `media-scan`)

- [ ] 8.1 RED: add `tests/video_info_tests.cpp` case naming a `.webm` file directly and asserting it is collected; verify: it fails today.
- [ ] 8.2 Extend `kVideoTypes` with `.webm`, `.m4v`, `.ts`, `.m2ts`, `.mpg`, `.mpeg`, `.3gp`; verify: 8.1 passes plus a directory-scan case finding `.webm`/`.mkv` and a case rejecting `.srt`/`.txt`.
- [ ] 8.3 Verify the two lists agree one way: every video extension in `kStoredMediaExtensions` (`src/pack/pack_types.h`) is a video input; verify: a case in `tests/media_scanner_tests.cpp` (tag `[media-scanner]`) comparing `videoinfo::videoExtensions()` against that array's video entries.

## 9. Packer: work directory and dead code (spec `pack-media-store`)

- [ ] 9.1 RED: add `tests/pack_execute_tests.cpp` case packing a root containing `.encro/job-state.json` and asserting no archive entry is under `.encro`, plus a case asserting `.config/app.json` is still packed; verify: the first fails today.
- [ ] 9.2 Filter `.encro` directories out of the packer's walk; verify: 9.1 passes, including a case where a subdirectory carries its own `.encro`.
- [ ] 9.3 Delete the unused `ZipWriter` struct and confirm the two `packFilesToZip` overloads still close their archive on every path; verify: `xmake build encro` plus the existing packer cases.

## 10. Prompt with no readable stdin (spec `console-output-conventions`)

- [ ] 10.1 RED: update `tests/utils_tests.cpp` — the interrupted-input, stop-request and prompt-shape cases now assert "no answer" instead of `false`, and a new case asserts an unavailable stdin stays out of the shared consent slot; verify: the updated cases fail against today's `bool`.
- [ ] 10.2 Change `readUserIpt`/`readUserIptOnce` to `std::optional<bool>` (`nullopt` = no answer) and keep the shared slot `true`/`false` only; verify: 10.1 passes.
- [ ] 10.3 Map `nullopt` at all three prompt call sites (`video_process.cpp`, `video_batch_execution.cpp`, `picture_process.cpp`) — stop pending keeps today's cancellation path, otherwise an error naming `--yes` and exit code 1; verify: one case per site asserting the message and the exit code, plus a case pinning that a typed `n` still prints the decline notice and exits 0.

## 11. Already-HEVC scan hint (spec `pipeline-narration`)

- [ ] 11.1 RED: add a `tests/video_info_tests.cpp` case scanning a directory where the fake ffprobe reports two HEVC files among five and asserting one hint line naming the count; verify: it fails today (only a debug log).
- [ ] 11.2 Return the dropped count from the HEVC filter and print the single hint from the directory-scan path; verify: 11.1 passes, a no-skip case prints no line, and the single-file message stays unchanged.

## 12. Dry-run counts (spec `image-character-organize`)

- [ ] 12.1 RED: add `tests/organize/pipeline_tests.cpp` case running a dry run where one image is already at its destination with identical content and another is not, asserting one skipped and one would-copy in the report; verify: it fails today (both counters are zero).
- [ ] 12.2 Count destination decisions in a dry run and word the totals line conditionally; verify: 12.1 passes, and the real-run report wording is unchanged.

## 13. Integration verification

- [ ] 13.1 Run the full unit suite and the e2e suite; verify: `xmake test-report` reports 0 failures and `xmake build e2e_tests && xmake run e2e_tests` passes.
- [ ] 13.2 Run the reporter-mode probe; verify: `build/windows/x64/release/tests.exe -r console -s` reports 0 failures.
- [ ] 13.3 Run `openspec validate fix-audit-findings --strict`; verify: valid.

## Planning-artifact review

One reviewer, two lenses, run before implementation; 10 findings, verdicts below. Round 1 fixed findings 1, 2, 5, 7, 9, 10; round 2 confirmed 3, 4, 6 fixed and 8's leftover Purpose wording, fixed after the round's report.

- [P1] The image-character-organize delta required an install marker the design refused to create, and the second-pass rename could leave a partial install that the presence check accepts. — **resolved**: the requirement now says the DLL the install check tests is renamed last, and no marker file is described.
- [P2] design.md described today's state commit two incompatible ways. — **resolved**: only the rename → remove → retry description remains, matching `job_state.cpp`.
- [P2] Task 3.1 was not a RED test: it used a helper that a later task introduced. — **resolved**: 3.1 is now the pure-move extraction, 3.2 the RED cases against it, 3.3 the semantics.
- [P2] The error-visibility requirement claimed to cover "every other directory walk" while naming three, leaving `organize/teach.cpp` and `picture/picture_process.cpp` unguarded. — **resolved**: the rule moved to its own ADDED requirement that names all six walks, and task 7.4 covers them.
- [P1] The console-output-conventions delta invented a cancellation code for a typed decline (`canceledExitCodeForPromptAbort` returns 0 with no stop pending). — **resolved**: the typed decline keeps exit 0, the no-answer path is the one that exits 1.
- [P1] Three prompt call sites, not two. — **resolved**: design, tasks and the proposal's impact name all three files.
- [P1] `.m2ts` is not in the pack whitelist, so the "exactly the same list" claim could not hold. — **resolved**: the video set is now stated as the whitelist's video entries plus `.m2ts`, and task 8.3 checks one-way containment.
- [P1] The identity inventory named "organize's folder keys" and `planVideoOutputFiles`, neither of which derives from `stablePathString`. — **resolved**: both left every inventory; the design states `path_map` is exact-case and needs no change.
- [P1] The settings set omitted `videoCodec`, contradicting its universal claim. — **resolved**: `videoCodec` joined the snapshot fields in the delta, design and task 2.2.
- [P2] The impact list and three verify clauses pointed at material that does not exist. — **resolved**: the impact names the suites and files the tasks touch, the missing cases became "add a case" tasks, the tag is `[media-scanner]`, and task 10.1 updates the existing `readUserIpt` cases that pin the removed behaviour.

## Workflow follow-up

- Run the code-review skill over the implementation before archiving.
- Archive the change with `openspec-archive-change` once the review is clean.
