# Tasks — cli11-native-validation

## 1. Baseline and native message capture

- [x] 1.1 Build and run the existing cmd test suites (`cmd_cmd`, `cmd_config_builder`, `cmd_help_tiering`) to confirm the green baseline
- [x] 1.2 Probe and pin the *no-change baseline* edge behaviors with throwaway tests: bare `expected(0,1)` flags (`--color`, `-f`, `-t`, `-j`, `--min-vmaf`, `--force-conflict-handling` — all silently act as the default today, verified), boundary values (`-q 2/31`, `--crf 0/51`, `-j 1`) — so the rewrite can be diffed against them; do NOT capture native error texts here (they depend on the final registration shape, see 5.1)
- [x] 1.3 Commit the planning artifacts (proposal/specs/design/tasks) as one `docs:` commit (English) before implementation, per project convention

## 2. Registration layer rewrite (src/cmd/cmd.cpp)

- [x] 2.1 Delete `CmdFlagDef`, the four flag-def arrays, `registerCmdFlag`, `buildXxxApplyMap`×4, `buildResultApplyMap`, `optRegistry`, `PendingExclusion` (incl. dead `excludesDesc`), `collectAdvancedLongNames`, and the post-parse copy loop
- [x] 2.2 Hand-register all 30 options (29 flag defs + positional) binding directly to `CmdParseResult` fields, in the same per-group registration order as the current def arrays (help order contract); copy option names, descriptions, and the positional name `input-paths` **verbatim** (tiering tests assert description substrings); apply the Decision-1 mapping rules (`optional` binding + `default_str` for default-less options, plain field + `expected(0,1)` + `default_str` for defaulted options, `-I`/positional as `optional<vector<string>>` with `expected(0, 1000000)`)
- [x] 2.3 Attach parse-time validators per Decision-2 table: `CheckedTransformer` for `-t` (vid/pic), `IsMember` for `-f`/`--force-conflict-handling`(ignore_case)/`--color`(ignore_case)/`--preset`, `Range` for `-q`/`--crf`/`--min-vmaf`, `PositiveNumber` for `-j`, `NonNegativeNumber` for preview `--start`/`--duration`; leave `--video-codec` unvalidated (open set — see design Non-Goals)
- [x] 2.4 Replace conflict/dependency machinery with direct `excludes(pointer)` calls (`-i`↔`-I`, `--resume`↔`--restart`, `-p`↔`-z`, `--dry-run`↔`--crf`, positional `input-paths`↔`-i` and ↔`-I`) and `needs(--compress)` on `--image-quality`
- [x] 2.5 Replace `collectAllAdvancedLongNames` with a standalone `constexpr std::array<std::string_view>` listing exactly the 10 currently-advanced options (the `.advanced=true` defs: verbose, log-json, full-progress, color, inputs, state-file, force-conflict-handling, ffmpeg-path, preset, video-codec — note the `cli-help-tiering` main spec enumerates only 9, missing `--video-codec`; the code list is authoritative) feeding the existing main formatter
- [x] 2.6 Dispatch `CLI::CallForHelp` in `parseAndPopulate`'s catch (help=true, help from `ex.get_app()->help()`, exit-0 semantics) before the generic `ParseError` branch

## 3. Preview subcommand native rewrite

- [x] 3.1 Rewrite `registerPreviewSubcommand`: bind all fields directly to `CmdParseResult` preview members, `original->required()`, `CLI::NonNegativeNumber` on `--start`/`--duration`, restore the native help flag via explicit `sub->set_help_flag("-h,--help", …)` (do not rely on construction-time inheritance); word the `original`/`encoded` descriptions so preview help still carries the `[<encoded>]` usage hint that the old hand-written message contained
- [x] 3.2 Add the preview-specific `formatter_fn` (Decision 4, option A): iterate `sub->get_options()`, reuse `formatOptionName`/`formatOptionHelp`/`wrapDescription`/`wrapDescriptionLine`/`resolveHelpTextLayout`; keep colored styling; do not reuse the parent's `makeHelpFormatter`
- [x] 3.3 Delete `buildPreviewHelpText` and `populatePreviewCommandResult` (required-check and reads now handled by CLI11 + binding)

## 4. buildConfig slimming (src/cmd/config_builder.cpp)

- [x] 4.1 Delete hand-written checks that moved to parse time: `readProcessType` (alias/whitelist), `readOutputFormat`, `readMaxParallelJobs`, `readForceNameConflictHandling`, the range/message checks in `applyMediaOptionValidations` (image-quality range, crf range, min-vmaf range, `--dry-run`+`--crf`, `--image-quality`-without-`--compress`), AND the `-i`↔`-I` + positional-mix conflict branches in `applyInputSelection` (now native `excludes()`); store canonical `processType` directly
- [x] 4.2 Keep and verify the remaining checks: filesystem validation, output-alias resolution, value-conditional checks (`--compress` with non-picture type, multi-input only for video, multi-input with `--pack-only`, missing-input summary), `nvencPreset == "auto"` reset; note that `prelude.cpp`'s `configureFromColorString` error branch becomes unreachable once `--color` is validated at parse time — leave it as harmless defense (or remove, but keep `parseColorMode`)

## 5. Test updates

- [x] 5.1 Add parse-time validation tests to `tests/cmd_cmd_tests.cpp` covering the spec scenarios: invalid enum values (`-f avi`, `--force-conflict-handling x`, `--color pink`, `--preset p9`), unknown `-t` value, ranges (rejections `-q 99`/`--crf 99`/`-j 0`/negative preview `--start` AND boundary acceptances `-q 2`/`-q 31`/`--crf 0`/`--crf 51`/`-j 1`/`--min-vmaf 0`/`--min-vmaf 100`), case-insensitive acceptances (`--force-conflict-handling Y`, `--color ALWAYS`), bare-flag defaults (`--color` alone ≡ default, `--min-vmaf` alone ≡ 95), conflicts (`--resume --restart`, `--dry-run --crf`, positional with `-i`/`-I`), `--image-quality` without `--compress`, preview missing `original`, preview `-h` exit-0 help; assert with the native texts captured from the first actual test run (pin them then freeze); ALSO update the two existing preview cases asserting the old hand-written messages / `result.preview` on error paths (preview flag is no longer set when the parse fails before `got_subcommand` handling)
- [x] 5.2 Add `-t vid`/`-t pic` alias-mapping tests at parse level (move from config_builder tests)
- [x] 5.3 Update `tests/cmd_config_builder_tests.cpp`: delete cases for checks removed in task 4.1 — including "rejects both input and inputs", "rejects positionals combined with -i", "rejects positionals combined with -I", the invalid-enum/range cases, and the alias-mapping cases (moved to 5.2); keep value-conditional, fs, and alias-resolution cases
- [x] 5.4 Check `tests/e2e/encro_e2e_tests.cpp` for CLI message-text assertions and update the four confirmed cases: "encro rejects positional input mixed with -i" (~L1031), "encro rejects out-of-range --min-vmaf" (~L2291), "encro rejects --dry-run combined with --crf" (~L2297), "encro preview with no positionals fails clearly" (~L2422, asserts `preview requires at least one positional argument`) — update assertions, do not delete the cases
- [x] 5.5 Confirm `tests/cmd_help_tiering_tests.cpp` passes with zero edits, diff `-h`/`-hh` output against the pre-change build via one-shot stash build (IDENTICAL), and remove the type column from the formatter entirely (structural guarantee: binding/validator type names can never render; the aligned-columns color test pins column positions)

## 6. Final verification and commit

- [x] 6.1 Full build + unit suite + e2e suite green (`xmake test-report`, `xmake test-parallel`)
- [x] 6.2 Run `xmake fmt -k` and `xmake tidy`; fix findings
- [ ] 6.3 Commit implementation + tests + tasks.md checkboxes in one commit (conventional message, English)