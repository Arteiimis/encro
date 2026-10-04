# Tasks: organize-existing-structure

## 1. Folder disposition module

- [ ] 1.1 Add the failing tests for the disposition pre-pass in `tests/organize/disposition_tests.cpp` (`[organize]`): default disposition is reference; `--ingest`/`--ignore-folder` names beat the list and the default; misc-list hits are input; dot-prefixed folders are ignored unless ingested; a recursive run defaults every first-level folder to input; a name given to both flags is rejected — then implement `src/organize/disposition.{h,cpp}` (`FolderDisposition`, the pre-pass walk) and verify `xmake test-report --tag="[organize]"` passes with the new cases red-first
- [ ] 1.2 Add the failing boundary tests for `kMiscFolderNames` matching (whole trimmed name, ASCII case-insensitive via `Misc`, CJK exact via `雜項` and `其他`, substring rejection via `夏mix子`, no simplified/traditional cross-matching), implement the matcher beside the constant, and verify the same test command passes

## 2. CLI surface

- [ ] 2.1 Add the failing command-parse tests for repeatable `--ingest`/`--ignore-folder` and the argument errors (name is not an existing first-level directory besides the output tree; same name on both flags), wire the options through `src/cmd/cmd.{h,cpp}` into `organize::Options`, and verify `xmake test-report` passes including the existing help-layout checks (`organize -h` shows both flags)

## 3. Scan input from dispositions

- [ ] 3.1 Add the failing `scanImages` tests: incremental (non-recursive) input = root loose images plus recursively collected input folders, never entering reference or ignored folders; a recursive run collects every first-level folder's images recursively (whole-set behavior unchanged); output-tree exclusion still holds — implement the disposition-driven input assembly in `src/organize/scan.cpp` and verify the organize test suite passes

## 4. Reference sampling and demotion

- [ ] 4.1 Add the failing sampling tests: members collected recursively, ordered and capped by content hash at `kReferenceSampleSize`; cache hits analyze nothing; analyses land in the shared cache and a second run performs no inference — implement the sampling stage in the pipeline with its own progress bar (`mediaitem::runStage`), and verify with the fake-engine tests
- [ ] 4.2 Add the failing demotion tests: a sample splitting into two clusters under `identityTau` demotes the folder (no folder-match reference, no tag ownership), a single-cluster sample does not, and demotion never changes disposition — implement by clustering sample profiles through the existing clustering entry point and dropping demoted folders before references are built, and verify the suite passes

## 5. References and routing

- [ ] 5.1 Add the failing teaching tests: a first-level reference folder teaches folder matching into `organized/<its name>/`; its sampled sole-tag majority owns the character tag; a first-level folder and an output-root folder of the same name merge into one union reference — extend `buildFolderReferences` in `src/organize/teach.cpp` per design D5 and verify the suite passes
- [ ] 5.2 Add the failing naming-collision tests: a new cluster whose descriptive name equals a first-level or output-root folder name receives a deterministic suffix — seed `usedNames` in `clusterRemainder` per design D7; add the failing execute test that a failed copy leaves no empty destination folder and implement the staging-copy-before-create reorder in `src/organize/execute.cpp` (strict lazy creation, design D7) — verify the suite passes
- [ ] 5.3 Add the failing tests for the incremental-entry notice: exactly one line naming the mode and reference count, printed by the command layer before engine loading (design D8), and no new notice on a recursive run — implement in `src/organize/organize_command.cpp` and verify with `ENCRO_DEBUG_DUMP_RENDER` where layout matters

## 6. Report

- [ ] 6.1 Add the failing report tests: the disposition summary lists every first-level folder as reference/input/ignored/demoted with the `--ingest` hint on demoted rows; zero-match reference folders appear as zero-count rows with their disposition as source; column alignment survives the new rows (long-name truncation still holds) — extend `src/organize/report.cpp` and `ReportData` per design D9, self-check with `ENCRO_DEBUG_DUMP_RENDER=<file> --color always` on a synthetic case, and verify the suite passes

## 7. End to end and docs

- [ ] 7.1 Add the e2e case in `tests/e2e/encro_organize_tests.cpp` (fake tools): a directory with a hand-made `角色A/` folder, a `mix/` folder and loose images — the run files matching loose and mix images into `organized/角色A/`, leaves `角色A/` and the originals untouched, creates no zero-match skeleton folder, and the second run analyzes nothing; verify `xmake build e2e_tests && xmake run e2e_tests` passes
- [ ] 7.2 Update `README.md`'s organize section (incremental behavior, dispositions, the two flags, the sampling cost note) and verify the README examples match `xmake run encro organize -h` output

## Review outcome (planning artifacts)

One fresh reviewer, two lenses; findings triaged and fixed, then verified by a second fresh reviewer (two fix→verify rounds, all resolved). Not yet committed — verdicts bind to the working-tree artifacts at implementation time.

- [P1] The kept "Default scan excludes subdirectories" scenario was falsified by the delta's own input rules (misc-named subfolder becomes input; a reference subfolder gets sampled). resolved (round 2) — WHEN qualified to a non-misc subdirectory, THEN admits profiling analysis and asserts never-routed/copied/modified; the scanning paragraph's "never be entered" collision became "never contribute scan input".
- [P1] The recursive scenario was self-vacuous under incremental-by-default (any image-bearing subdirectory implies a first-level folder), making `-r` permanently dead and losing the nested-gallery use case. resolved (restructure) — recursive runs keep whole-set behavior: every first-level folder is input, nothing mirrored; BREAKING claim removed from the proposal.
- [P2] The dot-prefix rule lived only in the delta/tasks; proposal said "Everything else is a reference" and design was silent. resolved — dot-prefix default-ignored stated in proposal, design D1 and the delta.
- [P2] The "first-level folders are never scan input" rationale contradicted ingest/misc folders being scan input. resolved — clause removed with the restructure.
- [P2] Impact placed the entry notice in pipeline.cpp, but it must print before engine loading, which happens in organize_command.cpp. resolved — pre-pass and notice assigned to organize_command.cpp (proposal Impact, design D8, task 5.3).
- [P2] Lazy-creation SHALL was violated by execute.cpp's create-before-copy (a failed copy left an empty folder) while the proposal claimed execute.cpp needs no change. resolved — staging-copy-before-create reorder planned (proposal Impact, design D7, task 5.2).
