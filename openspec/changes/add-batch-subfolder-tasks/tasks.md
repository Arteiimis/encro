# Tasks

## 1. Output redirection seam

- [ ] 1.1 Write a failing `jobstate` snapshot test (different set `packOutputDir` values must not match, equal set values must, and a snapshot saved without the field must still match a config with it unset — the absent-field compatibility pattern), then add the optional `packOutputDir` field to `ConfigSnapshot`, `buildConfigSnapshot`, serialization, and `configMatches`; verify the test passes and existing snapshot-matching tests stay green
- [ ] 1.2 Write a failing test for `resolveVideoPackOutputPath` honoring `config.packOutputDir` verbatim (unset keeps today's `outputPath|input / "packed"` behavior), add the optional `AppConfig::packOutputDir` field and the resolver branch; verify both cases pass
- [ ] 1.3 Write a failing fake-tool picture test asserting a picture pack run with `packOutputDir` set writes archives there (not under `<dir>/packed`), then apply the same seam in the picture workflow's output-dir resolution; verify it passes
- [ ] 1.4 Write a failing fake-tool pack-only test asserting a `-z` run with `packOutputDir` set writes archives there, then apply the seam in `runPackOnly` (`src/app/pipeline.cpp`); verify it passes

## 2. Batch driver core

- [ ] 2.1 Write failing unit tests for subfolder enumeration (first-level directories only, loose files ignored, hidden dirs / `.encro` / the resolved packed root excluded, name-sorted, empty result allowed), then implement the enumerator in the new `src/batch/` module; verify the tests pass
- [ ] 2.2 Write failing unit tests for the media presence pre-check mirroring the effective option combination (video encode → video extensions; picture + `--compress` → picture extensions; picture + `--video-webp` → pictures ∪ videos; picture plain pack → picture extensions; pack-only → any regular file), then implement it on exported scan predicates (export the module-private video set and function-local picture set as helpers); verify the tests pass
- [ ] 2.3 Write failing fake-tool unit tests for the sequential run loop (per-sub `AppContext` with `inputPath=<root>/<sub>`, `packOutputDir=<packedRoot>/<sub>`, fresh store per sub created exactly when the equivalent single-directory run would create one; done/failed/skipped classification; a failing sub does not stop later subs; exit code non-zero exactly when a sub failed; media-less sub skipped with notice), then implement the loop; verify the tests pass
- [ ] 2.4 Write failing stop-semantics unit tests (a sub's cancellation exit stops iteration and marks remaining subs skipped with the cancellation exit code; a stop requested between subs skips the next one), then implement the driver's stop checks using the existing `stopsignal` API; verify the tests pass with a fresh `ScopedStopSignalReset` per iteration

## 3. Batch summary

- [ ] 3.1 Write failing unit tests for the summary table (one row per enumerated subfolder with done/failed/skipped status, count line, stdout, `console-output-conventions` prefixes), then implement the renderer; verify the tests pass and self-check the output with `ENCRO_DEBUG_DUMP_RENDER` for stream/placement

## 4. CLI surface and dispatch

- [ ] 4.1 Write failing parse tests (`encro batch <root>` accepts the processing options for the chosen type; `-I`/positional-multi and `--state-file` rejected with native-style exclusion errors naming the options; `-o` alias forms (`+`, `=`, `input://`, `common://`) rejected as unsupported for batch; batch help renders), then register the subcommand in `cmd.cpp` following the `organize` pattern plus explicit exclusion stubs; verify the tests pass
- [ ] 4.2 Write failing unit tests for batch-config → per-sub-config mapping (options copied, `inputPaths` cleared, `outputPath`/`stateFilePath` nullopt, packed root default `<root>/packed` with `-o` taking a plain directory path), then implement the mapping; verify the tests pass
- [ ] 4.3 Write failing fake-tool tests for toolchain-once resolution (missing ffmpeg fails the batch with the tool check error before any sub runs; resolved paths shared across subs), then implement the up-front resolution and dispatch branch in `app_entry.cpp`; verify the tests pass

## 5. Integration and completion

- [ ] 5.1 Add an e2e case: real `encro` binary runs a picture-pack batch over three subfolders (one media-less) with the fake ffmpeg on PATH, asserting archives under `<root>/packed/<sub>/`, the skip notice, the summary table, and exit code 0; verify it passes
- [ ] 5.2 Add an e2e resume case: interrupt a batch mid-subfolder and re-run, asserting the finished sub performs no work and the interrupted sub resumes; verify it passes
- [ ] 5.3 Extend the completion registry tests to assert `batch` is offered at the main-command position and its options respect subcommand context; verify `xmake test-report` stays green

## Workflow follow-up

- Run the post-change review (`code-review` skill) with the change's spec path.
- Archive the change after review; verify the archived spec appears under `openspec/specs/`.

## Planning review record

Planning-artifact review (code-review skill, planning stage): one fresh reviewer, two lenses; 10 findings (3 Coherence, 7 Ground truth), all accepted and fixed in the artifacts; a fresh verifier then confirmed 10/10 resolved, one stale wording residual also fixed.

- [P1] C1 Skip pre-check designed around bare task type (video-webp / pack-only processability) → fixed: spec R3 reworded to effective option combination + two scenarios; design D5 mirrors per-combination scan; task 2.2 updated. resolved (planning review)
- [P2] C2 `batch -z` archive layout implemented by no task (third resolver untouched) → fixed: task 1.4 added for `runPackOnly` seam. resolved (planning review)
- [P2] C3 Native-style exclusion errors not produced by the organize pattern → fixed: design D1 adds explicit `-I`/`--state-file` stubs; task 4.1 updated. resolved (planning review)
- [P1] G4 Third hardcoded zip-dir resolver missed in design Context and proposal Impact → fixed: both name three call sites incl. `src/app/pipeline.cpp`; stale Risks wording corrected. resolved (planning review)
- [P2] G5 `videoInfoCache` sharing impossible (non-copyable AppContext) → fixed: design D2 drops sharing, per-sub fresh cache, re-probe risk accepted and recorded. resolved (planning review)
- [P2] G6 "Reuse the media scanner's extension sets" premise failed (sets unexported) → fixed: design D5 exports scan predicates as part of the seam; proposal Impact and task 2.2 updated. resolved (planning review)
- [P2] G7 Effective-path snapshot would discard all pre-existing states on upgrade → fixed: design D4 records the optional value only (absent-field compatibility); task 1.1 tests the absent case. resolved (planning review)
- [P2] G8 Unconditional per-sub job-state requirement vs `shouldEnableJobState` → fixed: spec R5 scoped to "created exactly when the equivalent single-directory run would create one"; proposal and task 2.3 aligned. resolved (planning review)
- [P2] G9 Unconditional toolchain requirement vs pack-only exemption → fixed: spec R10 scoped to per-type tool requirements including the exemption. resolved (planning review)
- [P2] G10 `-o` output-alias language undefined under batch → fixed: spec R4 + scenario reject alias forms; design D3 rejects at parse; tasks 4.1/4.2 updated. resolved (planning review)
