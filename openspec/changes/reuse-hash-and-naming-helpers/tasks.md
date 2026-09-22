## 1. File hashing on the shared helper

- [x] 1.1 Delete `fileHash` from `src/organize/scan.cpp:14-19` and call `core::sha256File` at its call site (`:46`, `ImageItem{.contentHash = ...}`); verify `xmake test-report --tag="[organize]"` passes and `rg -n "fileHash" src/organize/scan.cpp` returns nothing
- [x] 1.2 Delete `fileHashOf` from `src/organize/execute.cpp:37-42` and call `core::sha256File` at its call site (`:58`, collision resolution against an existing destination); verify `xmake test-report --tag="[organize]"` passes, including the different-content numeric-suffix case
- [x] 1.3 Replace the inlined read-and-hash block in `src/organize/teach.cpp:58-61` with `core::sha256File` **while keeping an explicit unreadable guard** (`if (digest.empty()) { continue; }`), because `teach.cpp:59`'s current path is `continue` and `""` is a reachable `AnalysisCache` key (`pipeline.cpp:141` puts under `items[index].contentHash`, which `scan.cpp:14-19` may have set to `""`, and `cache.cpp:138-149` has no empty-key rejection); verify a case covers an unreadable teaching member being **skipped** rather than matched against the `""` entry, and `xmake test-report --tag="[organize]"` passes
- [x] 1.4 Verify the other three sites genuinely need no new branch by reading them: `scan.cpp:46` stores the digest, `execute.cpp:59` guards on `!existingHash.empty()`, `model_store.cpp:183` compares against an expected value; record the three in the commit body and confirm `xmake test-report --tag="[organize]"` and `--tag="[tagger]"` pass
- [x] 1.5 Delete `fileHash` from `src/tagger/model_store.cpp:39-44` and call `core::sha256File` at its call site (`:182`, the downloaded-part digest); verify `xmake test-report --tag="[tagger]"` passes, including the checksum-mismatch case
- [x] 1.6 Verify the four wrappers are gone and the fifth block is deliberately kept: `rg -n "sha256Hex\(bytes\)" src` matches only `src/tagger/engine_factory.cpp:54`, and `rg -n "^auto fileHash|^auto fileHashOf" src` returns nothing

## 2. Shared naming planner

- [x] 2.1 Add the `planNamesByCandidate` case before the helper exists: a one-member group without force calls `uniqueName`; the same group with force calls `conflictName`; a multi-member group is sorted by `naming::stablePathString` and calls `conflictName` per member; verify the case fails to compile (red first) and note the compile error in the commit body
- [x] 2.2 Add `src/core/naming_plan.h` with the helper as specified in design.md D3; verify the 2.1 case passes
- [x] 2.3 Rewrite `planPictureZipEntryNames` (`src/picture/picture_process.cpp:88-136`) as a wrapper — keep the `OutputLayout::Keep` early return (`:96-104`) in front of the call, pass `Ty = std::string`, and keep `shouldForcePictureConflictNaming` as the predicate; verify `xmake test-report --tag="[picture-process]"` passes
- [x] 2.4 Rewrite `planVideoOutputFiles` (`src/video/video_output_planning.cpp:104-161`) as a wrapper — pass `Ty = fs::path`, keep `shouldForceConflictNaming` and keep the `ensureUniqueOutputPaths` call (`:157`) after the planner; verify `xmake test-report --tag="[plan-output]"` passes
- [x] 2.5 Verify the extraction changed no behaviour: `git diff --stat tests/` lists only the new helper case from 2.1 — `tests/video/video_output_planning_tests.cpp`, `tests/picture/picture_process_tests.cpp` and `tests/naming_strategy_tests.cpp` are unmodified

## 3. Verification & commits

- [x] 3.1 Run `xmake test-report` (full unit suite) and confirm zero failures
- [x] 3.2 Run `xmake build e2e_tests && xmake run e2e_tests` and confirm the encode/pack end-to-end flows still pass (they exercise both planners)
- [x] 3.3 Run `xmake test-parallel` and confirm every shard reports its assigned case count
- [x] 3.4 Run `xmake fmt` before committing and confirm a second run produces no further changes; run `xmake tidy` and confirm no new diagnostics on the touched files
- [x] 3.5 Commit the planning artifacts first as their own `docs:` commit (proposal, design, tasks, `.openspec.yaml`), then implementation + tests + ticked `tasks.md` in one `refactor:` commit (English, conventional, subject < 72 chars, body wrapped at 80); verify with `git log --oneline -2` that the split is docs-then-refactor
