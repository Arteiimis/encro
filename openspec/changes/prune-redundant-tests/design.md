## Context

The audit that motivated this change examined all 766 test cases against `src/` and produced a per-cluster inventory of deletions, merges, and moves, each justified by pointing at the surviving test that keeps the coverage. See proposal.md for motivation. This document records the decisions and the concrete inventory the implementation must follow. Line references are to the working tree at the time of the audit and are aids to locating tests, not a substitute for reading the test before cutting it: every deletion must be checked against its listed "kept by" survivor before removal.

## Goals / Non-Goals

**Goals:**

- Remove every test that cannot fail, tests only third-party behavior, and duplicate assertion homes — while keeping every distinct verified behavior.
- Make each behavior have exactly one strong assertion home (strongest available variant wins).
- Keep the suite green after every per-cluster commit.

**Non-Goals:**

- No production code changes, no new test framework, no reorganization of test directory layout beyond one new file.
- No new coverage beyond the one replacement test below (finding gaps is a separate effort).
- No changes to opt-in suite mechanics (`[real-ffmpeg]`/`[smoke]`/`[completion]` tags, `ENCRO_TEST_COMPLETION`, auto-skip behavior).

## Decisions

### D1: Zero-coverage-loss rule

A test is deletable only if (a) it cannot fail under any implementation, (b) it verifies third-party behavior with no project wiring involved, or (c) an identified survivor test asserts the same behavior with equal or stronger strength. "Stronger" means: more assertions over the same scenario, checks of exact values where the duplicate checks loose predicates (`>= 1`), or coverage of the superset scenario. When two copies have complementary assertions, merge (fold the unique assertions into the survivor) rather than delete.

### D2: CLI11 spike block — delete, with one project-level replacement

`tests/cmd_config_tests.cpp:415-544` (`cliParse` helper + the 7-case `[spike]` block) pins raw CLI11 `force_callback`/`default_str` semantics with no project code involved. Delete all 7 cases. The one project-visible behavior nothing else proves — a config-stored value must not trip CLI11 `needs`/`excludes` (e.g. stored `image-quality` without `--compress` must run) — is re-covered by a single new test in `cmd_config_tests.cpp` that stores `image-quality` in a config file, runs the real parse path without `-c`, and asserts success with the value applied. Alternative considered (keep 1-2 spike cases) rejected: project-level verification is what the suite is for, and it is robust to CLI11 upgrades.

### D3: Help-layout tests — invariants replace absolute constants

The auto-fit tests at `cmd_cmd_tests.cpp:443/466/509` assert magic column numbers (`descCol == 31/39/36`) computed from today's option names; any option rename breaks them without a real regression. Collapse the three into one test that computes the widest rendered long-flag cell from the help output itself and asserts the spec'd invariants (3-space gap after the widest cell, all descriptions aligned to the same column, per-tier/per-subcommand isolation). The verbatim commands-block pin at `:552` shrinks to brief-tier-only row presence (completion/config rows); the four usage-synopsis string assertions across `cmd_cmd_tests.cpp` and `cmd_help_tiering_tests.cpp` collapse to one home. Tier presence/absence tests in `cmd_help_tiering_tests.cpp` stay — they are the `cli-help-tiering` spec's verification.

### D4: `[fake-tool]` block destination — unit suite, not e2e

The 10 fake-tool contract cases (`encode_probe_tests.cpp:1012-1199`) move verbatim to a new `tests/fake_tool_tests.cpp` in the unit suite. They already run in the unit suite today using `FAKE_TOOL_EXE_PATH` + `exec2` + `ScopedEnvVar` from `tests/test_utils.h`, so the move needs no build-system change beyond adding the file to the unit target's sources, and the `portable-fake-tool` guarantee (process-spawning tests run everywhere) is unchanged. Moving them into the e2e target was rejected: different binary, more churn, no benefit.

### D5: Duplicate-collapse style — SECTIONs and table loops, no framework

Families of near-identical cases (e.g. six `fitPostfixWithEta` cases, nine terminal `MessageKind` style tests, three `readLastNLines` guard tests, the multi-input rejection pairs converging on one `config_builder` branch) become one `TEST_CASE` with `SECTION`s or a small local table loop, matching existing suite idiom. No Catch2 generators, no new helpers in `test_utils.h` unless a helper is used by 3+ collapses.

### D6: The 24-logger inventory becomes spot checks

`logging_infra_tests.cpp:130` copies the 24-tag array from `setup.cpp` — a pure change-detector. Replace with 3-4 spot checks (default logger + one logger per tier) plus the existing behavior assertions.

### D7: Commits are per-cluster, suite green between clusters

Seven batches, each committed separately (`test:` conventional commits), each leaving `xmake test-parallel` green: (1) meaningless/vacuous deletes across all clusters, (2) cmd cluster merges, (3) logging+infra cluster, (4) video cluster + fake-tool move, (5) picture/pipeline cluster, (6) pack/preview/naming cluster, (7) e2e cluster. Order puts mechanical deletes first so later merge diffs stay reviewable.

## Inventory

### I1. Delete outright — meaningless / cannot fail / tests-nothing

| Location | Test | Why |
|---|---|---|
| `tests/packer_standalone_compile_tests.cpp` (whole file, 8 ln) | — | All-comment TU, no include, proves nothing; remove from target sources |
| `tests/logging_crash_integration_tests.cpp:25` | crash message appears in per-run log via direct append | Crash simulated by test's own `std::ofstream`; no `crash::` code |
| `tests/logging_crash_integration_tests.cpp:151` | crash message format includes timestamp level and module tags | Tags written by test's own `std::format` (survivor: case at `:215` real format) |
| `tests/logging_infra_tests.cpp:95` | LOG_INFO source location injected | Circular: expected string built from the macro's own ingredients |
| `tests/logging_infra_tests.cpp:37` | tag constants use dot-notation | Constants equal themselves |
| `tests/logging_json_tests.cpp:390` | NDJSON output ends with newline | Asserts one literal `push_back('\n')` |
| `tests/logging_json_tests.cpp:335` | Empty source field when source_loc unavailable | Cannot distinguish the branch it names |
| `tests/logging_json_tests.cpp:360` | clone() returns independent instance | Never formats through the clone (delete; not fix) |
| `tests/app/pipeline_picture_tests.cpp:264` | can disable collision-safe names | Fixture filenames unique; flag cannot change outcome (strong form lives in `pipeline_pack_only_tests.cpp:127`) |
| `tests/app/pipeline_picture_tests.cpp:307` | keeps relative paths in keep mode | `>= 1` assertions only |
| `tests/e2e/encro_e2e_tests.cpp:1410` | console Ctrl+C availability probe is callable | `CHECK_NOTHROW` on probe that catches everything |
| `tests/video/video_batch_execution_tests.cpp:83` | videobatch types compile and are usable | TDD GREEN-phase scaffolding; every other test compiles these types |
| `tests/video/video_progress_parser_tests.cpp:197` | tail block longer than 12 lines | Constant is now 32; assertion cannot fail for the named reason |
| `tests/video/video_info_tests.cpp:275` | SECTION: negative avg_frame_rate propagates | ffprobe never emits it; source documents caller does not reject |
| `tests/naming_strategy_tests.cpp:23` | enum has correct values | Underlying ints never persisted |
| `tests/naming_strategy_tests.cpp:33`, `:39` | NamingConfig default / designated-init | C++ aggregate semantics |
| `tests/picture_compress_tests.cpp:166` | compressImageBatch empty input | `span{} -> empty vector` |
| `tests/picture_compress_tests.cpp:243` | success results alongside failures | Two separate batches concatenated; not mixed-outcome (survivors `:222` + `:176`) |
| `tests/infra/progress_tests.cpp:89` (both resolveColor cases) | resolveColor maps tones / falls back | Color lookup table |
| `tests/cmd_cmd_tests.cpp:80`, `:274` | --version / --image-quality not set by default | Duplicated verbatim inside "exposes defaults" (`:58`) |
| `tests/cmd_cmd_tests.cpp:253`, `:332`, `:345`, `:372` | --compress / crf / preset / jobs defaults | Struct initializers; `:372` self-admits "either way acceptable" and never checks jobs |
| `tests/cmd_config_builder_tests.cpp:114` | reads forced conflict handling flag | Duplicates defaults test `:65` |
| `tests/cmd_config_builder_tests.cpp:784`, `:809` | image-quality min 2 / max 31 | buildConfig no longer range-checks; boundary lives in `cmd_cmd_tests.cpp` Range tests |
| `tests/cmd_config_builder_tests.cpp:834` | leaves imageQuality unset | Duplicate of `:708` |
| `tests/cmd_config_tests.cpp:137` | negation without config equals absent | Trivially true for default-false `pack` |
| `tests/cmd_cmd_tests.cpp:829` | cli11 accepts all preset values | IsMember tautology; value list asserted in completion metadata tests |
| `tests/cmd_cmd_tests.cpp:409` | aligned columns when defaults colored | Subsumed by `:610` full stripped-text equality |
| `tests/cmd_cmd_tests.cpp:665` | help non-empty after color injection | `size() > 200` |
| `tests/cmd_config_tests.cpp:441-544` | `[spike]` block (7 cases) | See D2 |
| `tests/e2e/encro_e2e_tests.cpp:867` | rejects positional mixed with -i | Duplicate of unit `cmd_cmd_tests.cpp:940` |
| `tests/e2e/encro_e2e_tests.cpp:1846` | preview rejects missing input | Duplicate of `preview_process_tests.cpp:173` |
| `tests/e2e/encro_e2e_tests.cpp:476` | real-ffmpeg resume reuses segments | Own comment admits deterministic coverage elsewhere; residue near-tautological |
| `tests/logging_error_context_tests.cpp:205`, `:502` | 16-frame context/attr caps (both) | Source comments the cap unreachable (`logging.h:389`) |
| `tests/logging_json_tests.cpp:248`, `:271`, `:295`, `:316` | CJK / backslash / quotes / newlines escaping | boost::json::serialize behavior (project-side attr escaping at `error_context_tests:539` stays) |

### I2. Move — misplaced tests

| From | Test | To |
|---|---|---|
| `tests/video/encode_probe_tests.cpp:1012-1199` | 10 `[fake-tool]` cases + `runFakeTool`/`encodeArg` helpers | New `tests/fake_tool_tests.cpp` (unit target; see D4) |
| `tests/picture/picture_process_tests.cpp:19`, `:53` | `execute()` Media mode subPart split / baseName names | `tests/pack_execute_tests.cpp` |
| `tests/infra/toolchain_tests.cpp:9-25` | findFFmpeg / findFFprobe empty-dir cases | `tests/utils_tests.cpp` (folded into the resolve case at `:27`) |

### I3. Merge — duplicate assertion homes (delete the weaker copy)

**cmd cluster**
- `cmd_cmd_tests.cpp:130` + `:772` bare-invocation → keep `:130`, add `preview==false` check.
- `cmd_cmd_tests.cpp:259` + `:267` image-quality long/short → one test, two argv variants.
- `cmd_cmd_tests.cpp:287` + `:322` app-level flags before preview subcommand → one test, two flags.
- `cmd_cmd_tests.cpp:190` + `:197` removed flags → one test, two entries.
- `cmd_cmd_tests.cpp:351/358/365` missing option values → one SECTIONed test.
- `cmd_cmd_tests.cpp:229` + `:238` COLUMNS cap → one test, two sections.
- `cmd_cmd_tests.cpp:397` + `:627` no-ANSI (never / NO_COLOR) → one test, two sections.
- `cmd_cmd_tests.cpp:443/466/509` auto-fit constants → one invariant test (D3). `:574`+`:591`+`:674` usage ordering → one test. `:552` verbatim block → brief-tier row presence. `:636`/`:651` color-injection names/headers → fold into `:610`-adjacent coverage; delete surplus. `:689` min-vmaf default half → keep custom-value half only.
- `cmd_config_tests.cpp:193` preview twin display sync → fold into `cmd_cmd_tests.cpp:295` family (config delta already at `cmd_config_tests.cpp:40`). `:145` config row check → keep `--[no-]` collapse check only.
- `cmd_config_builder_tests.cpp:209` + `:370` multiple inputs forms → one SECTIONed test. `:229`+`:402`, `:247`+`:429`, `:270`+`:456` multi-input rejection pairs → three SECTIONed tests. `:289`/`:317`/`:344` single positional → one SECTIONed test. `:758`/`:685`/`:159` single-field pass-throughs → fold into `:650`. `:689`(dup of cmd_cmd `:689` default) → drop the duplicate half.
- `cmd_completion_command_tests.cpp:72` commands-section completion row → covered by shrunk `:552` block; delete. `:54` + `:66` completion help routing → two sections of one test.

**logging + infra cluster**
- `logging_json_tests.cpp:169` + `:407` + `:483` suffix extraction → keep `:483` (superset) + `:169`; delete `:407` (covered by `:124` elapsed + `:483`). `:75` level strings → trim to 2 levels. `:521` timestamp schema → keep only the `crash_runtime_tests.cpp:142` copy.
- `logging_error_context_tests.cpp` cases 1,2,5,6,7,9,11 (white-box mechanics) → merge to ~3 behavior-level cases; `:428/:458/:481` attribute push/pop/shadow → one SECTIONed test; D2-of-audit four static trait checks (`:89/:101`, `scoped_timer:124/:175`) → one traits test.
- `logging_scoped_timer_tests.cpp:68/:207/:249` move construct/assign/self-move → one SECTIONed test.
- `logging_file_mgmt_tests.cpp:177` + `:239` retention → sections of one test; `:77` + `:139` setup path → merge.
- `logging_snapshot_tests.cpp:41` empty-when-no-context → merge into state-table with `:77`-family cases; `:166` snapshot-after-error context asserts → trim (D-dup of `error_context:342`).
- `logging_crash_integration_tests.cpp:322` + `:373` json on/off direct write → sections of one test; `:241` timestamp char-indexing → trim to length-equality invariant.
- `infra/terminal_tests.cpp:78-163` MessageKind style/badge family → one table-driven test over MessageKind; Usage/OptionDesc/Version unstyled pairs → folded into the table.
- `infra/toolchain_tests.cpp` resolves-fails case absorbs the moved findFFmpeg cases.
- `infra/progress_tests.cpp:33/:56` pair (differ only by `ctx.tick()`) → merge; `:50` + `:77` smokes → one smoke; `:428-495` six `fitPostfixWithEta` → one SECTIONed test; four EtaEstimator sim harnesses → shared scaffolding, scenarios kept as data (11 → ~8 cases, scenarios preserved).
- `logging_run_id_tests.cpp` five micro-cases → two.

**video cluster**
- `encode_probe_tests.cpp:841` mid-layer skip-encode → delete (survivor `video_batch_execution_tests.cpp:565`). `:804` mid-layer unreachable-floor → delete (survivor `:503`). `:143`+`:164` decideCq ssim/xpsnr → one SECTIONed test. `:380` previewHint → fold into an existing probe case.
- `video_batch_execution_tests.cpp:226/:248/:270` barDone REQUIRE_NOTHROW-only trio → one smoke test (drop the lying names).
- `video_encode_runner_tests.cpp:198` + `:267` audio-extracted-once → keep `:267`, fold assembly checks in.
- `encode_config_tests.cpp:270` banner suppression test → fold three checks into webp case `:133`; trim duplicate NVENC-chain/hvc1 re-assertions in `:26/:308/:423` to delta-only.
- `video_output_planning_tests.cpp:350/:365/:385` resolveVideoPackOutputPath trio → one test, directory+file inputs (keep `:405` custom-output case).
- `video_progress_parser_tests.cpp:8/:25/:40` short/missing/empty → one SECTIONed test; `:107`+`:122` parse-over-large-file → delete (survivors `:76`/`:91` unit tests + wrapper logic at `:145`/`:239`); `:289`+`:300` progressPercent → one table test; `:145` rename (references removed "status" field).
- `video_quality_tests.cpp:65`+`:71`, `:147`+`:153` → one table-driven test per metric; `:94/:137/:196` parser failure trio → one SECTIONed test; `:60` empty-scores → extra CHECK in `:49`.
- `probe_cache_tests.cpp:10` six copy-pasted key mutations → (mutation, expectation) table.
- `video_info_tests.cpp:10`+`:27` 32MB boundary pair → one test, two inputs.

**picture / pack / preview cluster**
- Pipeline-vs-component (keep pipeline job-state/cache/resume cases; naming home is `pipeline_pack_only_tests.cpp`): delete `pipeline_picture_tests.cpp:30` (dup `picture_process_tests.cpp:94`), `:133` (dup `:111` — keep one, prefer the exact-assertion copy), `:347` (dup `:255`), `:414` (dup `:392`), `:484` merge with `picture_process_tests.cpp:523` (keep pipeline copy + add no-zip check), `:178` (weak vs `pipeline_pack_only_tests.cpp:100`), `:220` (weak vs `:154`), `:91`+`:378` (assertions don't verify their names; property covered by `naming_strategy_tests.cpp:157`).
- `picture_compress_tests.cpp` buildCMD four cases (`:29-102`) → 1-2; `:104`+`:149` success/rename → merge; `:176`+`:197` single/multi → merge into multi.
- `packer_tests.cpp:353` execute Directory mode → delete (survivor `pack_execute_tests.cpp:145`).
- `pack_service_tests.cpp:293`+`:488` missing-source → merge; `:327`+`:377` group callbacks → keep `:377`; `:151`+`:246` per-file progress ordering → merge (drop the 50ms-sleep variant); `:91`+`:125` selectPackPlanIndexes → merge; `:455` forced disambiguation → fold into `naming_strategy_tests.cpp:104` as a section.
- `pack_execute_tests.cpp:391`+`:434` summary entries → keep `:434`; `:367` default-strategy half → delete (run adds nothing over `:64`); `:288-323` cancel test accepts either outcome → tighten to the deterministic branch or fold into `:238`.
- `preview_filtergraph_tests.cpp:45-91` verbatim megatest → trim to the sub-chains the 11 targeted cases don't cover; `preview_process_tests.cpp:195` error re-verify → delete (unit-covered); `:256` single-window → fold into `:220`.

**e2e cluster**
- `:269` no-log-hint → fold into `:255`; `:277`+`:303` same run, complementary asserts → merge; `:371` spaced-path → section of `:330`; `:596`+`:922` failure state → two sections of one test; `:797`+`:831` positional forms → two sections; `:1590`+`:1639` summary statuses → fold as sections into `:1545` / `:1249`; `:1781` cache invalidation → fold into `:1724` (unit `probe_cache_tests.cpp` holds the logic coverage); `:1866` score-list printed once → fold into `preview_process_tests.cpp:97`; `:2008` real-ffmpeg single-input preview → fold into `:1960`; `:2111` config standalone → keep sections 3-4 (`--no-pack` override, malformed store), drop 1-2 (unit-covered).

## Risks / Trade-offs

- [Audit verdict wrong on some test] → Every inventory row names its survivor; the implementer re-reads the test and survivor before cutting, and any doubt downgrades delete → keep. Suite must be green per batch.
- [Coverage regression on some `src/` lines] → Only duplicate/vacuous cases removed; spot-check `xmake coverage --summary` after batches 4-5 (video, picture) where most orchestration deletions land; `portable-fake-tool`'s double-digit orchestration requirement must hold.
- [Opt-in suites silently broken by edits] → e2e batch preserves tags/env gates; run `ENCRO_TEST_COMPLETION=1 xmake test-report --tag="[completion]"` once after batch 7.
- [Batch 1 deletes feel unsafe without survivors] → They are the can't-fail set; if any turns out to fail after an unrelated edit, it wasn't in this class — restore it and file the finding.
