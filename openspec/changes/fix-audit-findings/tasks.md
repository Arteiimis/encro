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

- [x] 8.1 RED: add `tests/video_info_tests.cpp` case naming one file per new container directly and asserting each is collected, plus a `.srt` that stays out; verify: the `.webm`/`.m4v`/`.ts`/`.m2ts`/`.mpg`/`.mpeg`/`.3gp` rows failed before 8.2 (`readAllVids` returned empty).
- [x] 8.2 Extend `kVideoTypes` with `.webm`, `.m4v`, `.ts`, `.m2ts`, `.mpg`, `.mpeg`, `.3gp`; the same set applies to a named file and to a directory scan because both go through `media::extensionMatches`. Verify: 8.1 passes, and the existing directory-scan cases stay green.
- [x] 8.3 Verify the two lists agree one way: `kStoredMediaExtensions`' video entries are spelled out as `kVideoStoredExtensions` (`src/pack/pack_types.h`), and a `[media-scanner]` case in `tests/video_info_tests.cpp` asserts every one of them is a video input — plus that the whitelist still carries each of them, so a stale copy cannot make the check vacuous.

## 9. Packer: work directory and dead code (spec `pack-media-store`)

- [x] 9.1 RED: add `tests/pack_execute_tests.cpp` case planning a pack of a root holding `.encro/job-state.json`, a nested `sub/.encro` and `.config/app.json`, asserting no planned entry is under any `.encro` while `.config/app.json`, `.gitignore` and `sub/clip.mp4` are planned; verify: it failed before 7.3's walk change (every `.encro` file was planned).
- [x] 9.2 Filter `.encro` directories out of the packer's walk (`media::listFiles`' skipped-name list); verify: 9.1 passes, including the nested case, and dot entries that are not `.encro` stay packed.
- [x] 9.3 Delete the unused `ZipWriter` struct; verify: `xmake build tests` plus the existing packer and pack-execute cases stay green (the struct had no callers; both `packFilesToZip` overloads close their archive directly).

## 10. Prompt with no readable stdin (spec `console-output-conventions`)

- [x] 10.1 RED: update `tests/utils_tests.cpp` — the interrupted-input, stop-request and prompt-shape cases now assert "no answer" instead of `false`, and a new case asserts an unavailable stdin stays out of the shared consent slot; verify: the updated cases failed against today's `bool` (compile), and the new case pins the slot staying empty at EOF while a real answer still fills it.
- [x] 10.2 Change `readUserIpt`/`readUserIptOnce` to `std::optional<bool>` (`nullopt` = no answer), add `unansweredPromptNotice(prompt)` naming the question and `--yes`, and keep the shared slot `true`/`false` only; verify: 10.1 passes.
- [x] 10.3 Map `nullopt` at all three prompt call sites (`video_process.cpp`, `video_batch_execution.cpp`, `picture_process.cpp`): a stop pending keeps today's cancellation path, an unreadable prompt becomes an error naming `--yes` with the ordinary failure exit. Verify: a new `[video-batch-execution]` case asserts the message and `promptUnanswered` while the existing decline case now asserts a typed `n` still yields `canceled`, and the `[batch]` stop-during-prompt case still asserts the cancellation path end to end — that case caught the first cut of this change mapping a stop to a failure, which is exactly the distinction the requirement is about. The video pipeline's packing prompt (`video_process.cpp`) maps the same way but has no case of its own: reaching it needs a completed encode batch.

## 11. Already-HEVC scan hint (spec `pipeline-narration`)

- [x] 11.1 RED: add a `tests/video_info_tests.cpp` case scanning a directory of five clips the fake probe reports as HEVC and asserting exactly one hint line naming the count and the reason, plus a no-skip section asserting no hint at all; verify: both went red before 11.2 (no line printed), and the first cut printed nothing because `Info` messages go to stdout while the requirement puts the hint on stderr — the hint uses `MessageKind::Hint`, which is the severity kind that lands on stderr.
- [x] 11.2 Return the dropped count from the HEVC filter (`finalizeVideoList`'s out-parameter, threaded through `finalizeScannedVids`) and print the single hint from `readAllVids`' directory branch only, so a file named on the command line keeps its existing message; verify: 11.1 passes and the existing scan suites stay green.

## 12. Dry-run counts (spec `image-character-organize`)

- [x] 12.1 RED: add a `tests/organize/pipeline_tests.cpp` case that runs a real organize, adds one new image, then dry-runs: one would-copy, two already-there, and the rendered totals line reading `would copy`/`would skip`; verify: it went red before 12.2 (both counters were zero and the line claimed `copied 0`).
- [x] 12.2 Resolve the destination identically in a dry run (the identity check included), count the decision, add `ReportData::dryRun` and word the totals line conditionally; verify: 12.1 passes, the real-run wording stays byte-identical, and the two existing dry-run cases (`tests/organize/pipeline_tests.cpp`, `tests/organize/stage_tests.cpp`) now assert the counted destination instead of a zero copy count.

## 13. Integration verification

- [x] 13.1 Run the full unit suite and the e2e suite; verify: `xmake test-report` reports 0 failures (17597 assertions) and `xmake run e2e_tests` passes (876 assertions, 59 cases).
- [x] 13.2 Run the reporter-mode probe; verify: `build/windows/x64/release/tests.exe -r console -s` reports 0 failures (17584 assertions, 13 explicitly skipped).
- [x] 13.3 Run `openspec validate fix-audit-findings --strict`; verify: valid. Two flakes surfaced here and were fixed rather than retried: the state-file commit now tries one replacing rename first (no window where the state path is absent, and readers no longer lose a race with a timer-driven rewrite), and `testutils::readTextFile` retries its open until the deadline instead of failing on a single lost race; the e2e prompt case was rewritten for the new no-answer contract, and the job-state failure-injection fixture now blocks both the state path and the backup slot.

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

## Verify pass (openspec-verify-change, before the code review)

No CRITICAL. Three warnings, all resolved here before the code review opened:

- **W1** — the new packer case created only `a.bin` and asserted two entries, so on a case-sensitive filesystem the second entry was dropped and the case failed: `.github/workflows/ci.yml` runs the unit binary on ubuntu-24.04, so this would have gone red there. Resolved: `tests/packer_tests.cpp` now writes both spellings (one file on Windows, two elsewhere).
- **W2** — the delta promised a warning for an unreadable *entry*, which the walk cannot deliver: `skip_permission_denied` suppresses the error rather than surfacing it, and the POSIX case asserting that warning would have failed on Linux. Resolved on both sides: the requirement and its packer scenario now say the walk skips what it cannot read and continues (a warning is promised for a failure the walk itself reports), and the case asserts continuity plus the entries it could read.
- **W3** — the organize pre-pass scenario promised the run would name the unreadable entry, while that walk reports the directory it failed to list. Resolved by rewording the scenario to the behaviour the code has: the failure is recorded and the run continues with what it could read.

Reported, not fixed: S1 (design D2 does not mention the replacing rename that now runs first — added to D2) and S2 (three scenarios pinned indirectly: the directory-scan container case, the resume half of the path-identity scenario, and the cuDNN write-failure trigger).

## Code review (tier L: Standards / Spec / Leanness, one reviewer each)

16 findings, no P0/P1. Two fix rounds: the first recorded four findings as fixed that were not (see the note at the end of this section), the verification round caught that, and the second round made all of them real. Verdicts below describe the tree after the second round.

**Fixed**

- Spec P2 — the HEVC hint fired for a file named directly too, so that filter printed two stderr lines while the spec says one and D10 says directory roots only. Fixed: the guard now also requires `fs::is_directory(dirPath)`.
- Spec P2 — the proposal still claimed picture quality joins the snapshot after D1 dropped it. Fixed in the second round: proposal.md's fix bullet and capability line now name the codec/quality/preset/floor and say why picture quality stays out.
- Spec P2 — "SHALL NOT empty the state path" read absolute while the locked-target fallback does move the file aside. Fixed in the second round: the requirement now scopes the guarantee to a completed commit, says the staged snapshot replaces the file in one rename, and keeps the fallback described.
- Spec P2 — "batch subfolder discovery" promised a warning, while that walk reports a failed root as an error. Fixed in the second round: the requirement states that an unlistable root is that walk's own error, and the header comment on `media::walkDirectory` says why that walk keeps its own loop.
- Standards P2 — assertion inside a `StderrCapture` window in the new pipeline case. Fixed: the run result is captured, the `REQUIRE` runs after the window.
- Standards P2 — `template<class TyCollect>` broke the `Ty` rule. Fixed: the template is gone; the walker is a plain function.
- Standards P2 — one function in two return styles (`.h` trailing, `.cpp` prefix). Fixed: the four definitions use the trailing form.
- Standards P2 — the prompt verdict policy was restated at four call sites (two byte-identical). Fixed: `utils::classifyPromptAnswer` returns `PromptOutcome`, every site switches on it once.
- Standards P2 + Leanness P2 — `report.cpp` had two copies of the same totals format. Fixed: one format with conditional verbs.
- Standards P2 + Leanness P2 — `kVideoStoredExtensions` restated the whitelist's video block. Fixed: the video list is the single source and the whitelist is built from it (`concatExtensions`).
- Standards P2 — the `minVmaf` default was written three times. Fixed: `appctx::kDefaultMinVmaf`.
- Standards P2 — `allFilesPresent` lost its last production caller; `downloadMissing` kept a bare block. Fixed: both deleted.
- Leanness P2 `[reuse]` — four hand-rolled guarded walks while `media::walkDirectory` stayed private. Fixed: the walker is public (`WalkOptions` + an entry and a failure callback) and the disposition pre-pass, reference sampling, picture cache sweep and tool search call it.
- Leanness P2 `[merge]` — `pathIdentityString` re-implemented `stablePathString`'s fold. Fixed: on Windows it calls `stablePathString`.
- Leanness P2 `[delete]` — `finalizeVideoList`'s `skippedHevcCount = nullptr` default had no caller left. Fixed: the parameter is required.

**Rejected**

- Standards P2 — `ensureCudnn` exceeds clang-tidy's function-size threshold; the suggestion was to extract the staged-rename loop. Rejected: the loop is cohesive (install the staged set, marker last) and extracting it would exist only to satisfy a size metric, which the leanness rules forbid; six other pre-existing functions in this repo carry the same tidy warning.
- Leanness P2 `[merge]` — `kVideoStoredExtensions` deleted in favour of `std::span{kStoredMediaExtensions}.first(12)` in the test. Rejected as stated (it trades one duplication for a vacuity check that can no longer fail when the block is reordered); the same finding was accepted in its other form — the whitelist is now derived from the video list, so there is one source and no copy.

**Verification round (fresh reviewer, findings + fix diff)**

Verdicts: 12 resolved; 3 not resolved (the proposal and the two spec sentences above — recorded as fixed while no artifact had been touched); the two rejections judged defensible.

Second round, from the same report's P2 notes:

- The walker's `onEntry` now returns `bool`, so `findTool` stops at the tool it found instead of walking the rest of the install tree, and its warning no longer fires after a success.
- The unanswerable encode prompt logs `LOG_ERROR` again, so the failure reaches the run's log file and not just the console.
- A named already-HEVC file is pinned by a new section asserting the hint does **not** print for it.
- `kStoredMediaExtensions` is gone: the whitelist is `isStoredMediaExtension` over the video list plus the audio/image list, so there is no concat helper, no second array, and the containment case now also asserts the other half carries no video container.
- The `minVmaf` literal stays in `src/cmd/cmd.h`: moving it needs a core header in the CLI layer, which inverts the dependency; `kDefaultMinVmaf`'s comment now says so instead of claiming to be the only spelling.

**Recorded, not fixed**

- Design D2 does not mention that a commit now tries one replacing rename first — added to D2 (see the verify section).
- Three scenarios are pinned indirectly: the directory-scan container case, the resume half of the path-identity scenario, and the cuDNN write-failure trigger (the case drives a rename failure instead).
- `ensureCudnn` still exceeds clang-tidy's function-size threshold; rejected as a smell-only flag (see Rejected).

## Workflow follow-up

- Run the code-review skill over the implementation before archiving.
- Archive the change with `openspec-archive-change` once the review is clean.
