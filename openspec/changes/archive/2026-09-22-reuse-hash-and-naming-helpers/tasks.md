## 1. File hashing on the shared helper

- [x] 1.1 Delete `fileHash` from `src/organize/scan.cpp:14-19` and call `core::sha256File` at its call site (`:46`, `ImageItem{.contentHash = ...}`); verify `xmake test-report --tag="[organize]"` passes and `rg -n "fileHash" src/organize/scan.cpp` returns nothing
- [x] 1.2 Delete `fileHashOf` from `src/organize/execute.cpp:37-42` and call `core::sha256File` at its call site (`:58`, collision resolution against an existing destination); verify `xmake test-report --tag="[organize]"` passes, including the different-content numeric-suffix case
- [x] 1.3 Replace the inlined read-and-hash block in `src/organize/teach.cpp:58-61` with `core::sha256File` **while keeping an explicit unreadable guard** (`if (digest.empty()) { continue; }`), because `teach.cpp:59`'s current path is `continue` and `""` is a reachable `AnalysisCache` key (`src/organize/pipeline.cpp:141` puts under `items[index].contentHash`, which `scan.cpp:14-19` may have set to `""`, and `cache.cpp:138-149` has no empty-key rejection); verify a case covers an unreadable teaching member being **skipped** rather than matched against the `""` entry, and `xmake test-report --tag="[organize]"` passes
- [x] 1.4 Verify the other three sites genuinely need no new branch by reading them: `scan.cpp:46` stores the digest, `execute.cpp:59` guards on `!existingHash.empty()`, `model_store.cpp:183` compares against an expected value; record the three in the commit body and confirm `xmake test-report --tag="[organize]"` and `--tag="[tagger]"` pass
- [x] 1.5 Delete `fileHash` from `src/tagger/model_store.cpp:39-44` and call `core::sha256File` at its call site (`:182`, the downloaded-part digest); verify `xmake test-report --tag="[tagger]"` passes, including the checksum-mismatch case
- [x] 1.6 Verify the four wrappers are gone and the fifth block is deliberately kept: `rg -n "sha256Hex\(bytes\)" src` matches only `src/tagger/engine_factory.cpp:54`, and `rg -n "^auto fileHash|^auto fileHashOf" src` returns nothing

## 2. Shared path comparator

This section originally planned a shared naming *planner*. The Post-Change Review measured that extraction at +24 source and +94 test lines to fold away 33 lines of dispatch, so section 4 took the design's pre-authorised `D3` fallback and reverted it. What shipped is the one rule that was genuinely shared.

- [x] 2.1 Add `collisionnaming::stablePathLess` to `src/core/collision_naming.h:26` beside `stablePathString`, which it calls; verify it needs no include the header lacks
- [x] 2.2 Point the four sites that sort paths by `stablePathString` at it: `planVideoOutputFiles` (`src/video/video_output_planning.cpp:147`), `planPictureZipEntryNames` (`src/picture/picture_process.cpp:120`), `collectFolderSummaryPictures` (`src/picture/picture_process.cpp:188`) and the fingerprint input ordering (`src/core/job_state.cpp:542`); verify `xmake test-report --tag="[plan-output]"`, `--tag="[picture-process]"` and `--tag="[job-state]"` pass
- [x] 2.3 Verify the revert restored the planners: `git diff 3a9b971^ -- src/video/video_output_planning.cpp src/picture/picture_process.cpp` shows only the three `std::ranges::sort` lines, and `src/core/naming_plan.h` / `tests/naming_plan_tests.cpp` are gone
- [x] 2.4 Verify the swap changed no behaviour: `tests/video/video_output_planning_tests.cpp`, `tests/picture/picture_process_tests.cpp` and `tests/naming_strategy_tests.cpp` are unmodified and green

## 3. Verification & commits

- [x] 3.1 Run `xmake test-report` (full unit suite) and confirm zero failures
- [x] 3.2 Run `xmake build e2e_tests && xmake run e2e_tests` and confirm the encode/pack end-to-end flows still pass (they exercise both planners)
- [x] 3.3 Run `xmake test-parallel` and confirm every shard reports its assigned case count
- [x] 3.4 Run `xmake fmt` before committing and confirm a second run produces no further changes; run `xmake tidy` and confirm no new diagnostics on the touched files
- [x] 3.5 Commit the planning artifacts first as their own `docs:` commit (proposal, design, tasks, `.openspec.yaml`), then implementation + tests + ticked `tasks.md` in one `refactor:` commit (English, conventional, subject < 72 chars, body wrapped at 80); verify with `git log --oneline -2` that the split is docs-then-refactor

## 4. Post-Change Review follow-up

The review ran the Standards / Spec / Leanness axes as parallel sub-agents, and an independent verification agent checked every fix. It changed the change's scope, so the outcome is recorded here.

- [x] 4.1 Record the review verdict: one hard Standards breach (`auto isLocked() const -> bool` for a scalar, against `AGENTS.md:25`) and one Spec finding (the new teaching case never asserted its premise, so a lock that failed to block the read would have let it pass vacuously); both fixed in `22c1b20` and confirmed **resolved** by a fresh verification agent
- [x] 4.2 Take the design's pre-authorised `D3` fallback: revert the shared planner (`src/core/naming_plan.h` and its 94-line test deleted, both call sites back to their original inline loops) and keep the hash dedup; reconcile proposal.md (`What Changes`, `Impact`) and design.md (`D2`, `D3`, `D5`, `Risks`)
- [x] 4.3 Keep the `stablePathString` sort despite the leanness axis calling it unobservable: the verification agent confirmed it reproduces the original insertion order, and `summaryOutputDir` reads that order through `unordered_map::begin()` (`src/video/video_process.cpp:281`), so deleting it would have been a behaviour change; record the latent `begin()` defect in `docs/backlog.md` (`4cd6b2d`)
- [x] 4.4 Reject the remaining leanness and standards judgements with their reasons: `appctx::path_map` would drag `boost/json` into a low-dependency path header; the teaching case has no portable equivalent of an enumerable-but-unreadable file (`chmod` applies to the test process too); the `execute.cpp` anonymous-namespace merge is the natural consequence of deleting `fileHashOf`
- [x] 4.5 Re-run the full battery after the revert — `xmake test-report`, `xmake build e2e_tests && xmake run e2e_tests`, `xmake test-parallel`, `xmake fmt` (idempotent) and `xmake tidy` — and confirm no new diagnostic on any touched TU
