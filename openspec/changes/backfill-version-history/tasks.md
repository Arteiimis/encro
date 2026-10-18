# Tasks

## 1. Gate and public API declaration

- [x] 1.1 Write `docs/public-api.md`: the CLI subcommands and flag names with their accepted value sets, process exit codes, `config.json` keys and value types, the default `job-state.json` path and its persisted format, and the `--log-json` field names; state explicitly what is *not* judged (internal interfaces, test tasks, error wording, terminal rendering, newly written output files). Verify: every surface listed exists in the tree today — `encro -hh` and `encro config list` output, `src/cmd/config_store.cpp`'s key table, and the NDJSON field names in the logging code each match a line of the document; `rg` finds no listed flag absent from `src/cmd/`.
- [x] 1.2 Close the gate: `openspec list` shows no change other than this one, `main` equals `origin/main`, every working tree is clean, and no subagent is mid-run. Stale `refs/agents/**` checkpoints from finished sessions are **allowed and expected** — record how many exist and confirm each shares no ancestor with `main` (`git merge-base main <ref>` prints nothing); no step in this change reads them, because every anchor and check uses `main`'s `--first-parent` and the mirror is cloned from `origin`, which carries none. Verify: the five checks' output is recorded in this task's completion note; if any fails, stop and report rather than proceeding.

### Group 1 record

- **1.1 sources.** Flags were read from `encro -hh` and `encro <subcommand> -hh` (all six apps), keys from `encro config list` and `kCanonicalKeyOrder` (`src/cmd/config_store.cpp:32-49`), the state shape from `toJson(Snapshot)`/`toJson(TaskRecord)`/`toJson(ConfigSnapshot)` (`src/core/job_state.cpp:248-380`), the NDJSON fields from `src/logging/json_formatter.h` plus the `task_id`/`input` attributes stamped in `src/core/task_executor.cpp:38-45`, and the default state path from `work_dirs::jobStateFile` (`src/core/work_dirs.cpp:107`).
- **1.1 checks.** 51 distinct `--flag` tokens appear in the document and every one matches text inside `src/` (0 missing). The 18 config keys match `kCanonicalKeyOrder` item for item **in order** (compared positionally, 18 vs 18, no mismatch). Each key's declared JSON type agrees with the binding its option uses — `cfg::Path{}` / string for `ffmpeg-path` and `model-dir`, `{false}` flags for the six booleans, `cfg::ConfigKey` for the rest. The 9 formatter-emitted NDJSON fields (`timestamp`, `level`, `module`, `source`, `message`, `run_id`, `error_context`, `summary`, `elapsed_ms`) all appear in the document, and every `job-state.json` field name emitted by the three `toJson` overloads is listed. Exit codes read `0` from `app_entry.cpp:98`, `1` from `failRun` (`app_entry.cpp:124`), and `130` from `kCanceledExitCode` (`src/infra/stop_signal.h:7`).
- **1.2 gate record (2026-10-18).** `openspec list` → only `backfill-version-history`. `main` = `origin/main` = `73cf9ae3b0f3`. Both working trees sit at `73cf9ae3b0f3` with no tracked changes (only the untracked `docs/public-api.md` this task wrote). No subagent mid-run: the newest `refs/agents` checkpoint is dated 2026-10-10, eight days old. 8 checkpoint refs exist and every one reports **no common ancestor** with `main`, confirming they are the dead-session artefacts D9 describes rather than live state.

## 2. Anchor archaeology (produces the tag list; no tags yet)

- [x] 2.1 Locate the three 0.x anchors on the post-2026-10-08 history by feature cluster only — `v0.2.0` at the first `crash_runtime` commit, `v0.3.0` at the output-layout / zip-name-collision / pack-grouping cluster, `v0.4.0` at the resumable-pack + job-state + e2e cluster. Verify: each anchor's `git show --stat` contains the named subsystem and its date matches the 2026-10-05 exploration's month (`~03/05`, `~04/13`, `~04/19`); the three form an ancestry chain.
- [x] 2.2 Locate the ten original tags and restore each one's recorded message verbatim from `design.md`'s Appendix — recovered text, not re-authored, and no longer than the recorded first line. Their recorded names are two-part (`v1.0`…`v1.7`) and the rebuilt tags must be three-part (`v1.0.0`…`v1.7.0`); `v0.1.0` and `v0.1.5` were lightweight tags, so their message is newly written and marked as such, not recovered. Verify: each anchor's commit resolves and its date is **at or before** the date the Appendix records for that tag's creation, and it is the milestone's functional close point (the `chore: archive vX.Y` commit, the `merge: vX.Y milestone` commit, or the last functional commit of the milestone when neither exists); the eight annotated messages equal their Appendix line byte for byte; and the two 0.x messages are recorded as newly written rather than compared against the Appendix's "no tag message" line. **The three later tags break the simple date match and must say so in the table**: `v1.4`–`v1.6` were created on 05-23 in one backfill pass (`c5d3171`, `9ba6f0a`) although their milestones closed 05-01 / 05-05 / 05-10, so the Appendix date is the tag's creation date, never the anchor's.
- [x] 2.3 Locate the four post-`v1.7.0` anchors at the boundaries D3 fixes — the last commit before `v2.0.0-alpha.1` at the 08-03 `cli-help-tiering` boundary, `v2.0.0-alpha.2` at the 08-19 `consolidate-intermediate-files` landing (`e94d3fc`) rather than its 08-26 archive, `v2.0.0-alpha.3` at the 09-28 `identity-embedding` boundary (which also covers 09-26's exit-code change), `v2.0.0-rc.1` at the 10-05 `organize-existing-structure` archive, and `v2.0.0` at HEAD. Record for each whether it is the change's `docs: archive` commit or its landing commit and why, since they differ by up to a week. Verify: the list satisfies D4 using **the chosen commit's own date** — date, ancestry and precedence ascend together as `alpha.1 < alpha.2 < alpha.3 < rc.1 < 2.0.0` — and no anchor is an arbitrary commit inside a span.
- [x] 2.4 Paste the complete anchor table (version, commit, commit date, tag-creation date where the Appendix records one, subject, source of the decision) into this file as a review block, and state the total as `10 rebuilt + 3 early 0.x + 4 pre-release + 1 normal release = 18`. Verify: every row resolves with `git show -s`, no row's commit is an ancestor of the row above it, and the changelog section count planned in group 3 equals the 18 tags this table states.

### Anchor table (task 2.4 — review block)

| Version | Commit | Commit date | Recorded tag date | Subject | Why this commit |
|---|---|---|---|---|---|
| `v0.1.0` | `fab2517` | 2026-02-27 | 2026-02-27 | `refactor(utils): remove unnecessary blank line in utils.cpp` | Appendix subject matches exactly |
| `v0.1.5` | `6ee0657` | 2026-03-02 | 2026-03-02 | `refactor(video): replace std::vector with std::span for input paths` | Appendix subject matches exactly |
| `v0.2.0` | `e0dad58` | 2026-03-05 | — | `feat(crash): implement crash handling and stacktrace reporting` | last commit of the verbose / max-parallel / crash-handler cluster, and the last commit of March |
| `v0.3.0` | `aa5fcec` | 2026-04-13 | — | `feat(pack): enhance grouping logic to maintain source directories together` | closes the output-layout (04-12) + zip-name-collision (04-12) + pack-grouping (04-13) cluster |
| `v0.4.0` | `ee2b340` | 2026-04-19 | — | `feat(help): add short help hint for invalid CLI arguments` | closes the resumable-pack + job-state + e2e + immer-cache cluster of 04-18..04-19 |
| `v1.0.0` | `4920075` | 2026-04-26 | 2026-04-26 | `chore: archive v1.0 milestone` | the milestone's own archive commit |
| `v1.1.0` | `0c30797` | 2026-04-27 | 2026-04-27 | `feat: v1.1 Lambda Readability Refactor — 10 lambda functions extracted` | milestone named in the subject; it has no archive commit |
| `v1.2.0` | `3867dcf` | 2026-04-29 | 2026-04-29 | `chore: archive v1.2 milestone files` | the milestone's own archive commit |
| `v1.3.0` | `0128f36` | 2026-04-30 | 2026-04-30 | `chore: archive v1.3 milestone files` | the milestone's own archive commit |
| `v1.4.0` | `c93a92a` | 2026-05-01 | **2026-05-23** | `feat(v1.4): PackRequest declarative API & IPacker abstraction removal` | milestone named in the subject; it has no archive commit. **Tag was backfilled 05-23** |
| `v1.5.0` | `2981691` | 2026-05-05 | **2026-05-23** | `merge: v1.5 milestone — Pack Refinement complete (+audit)` | milestone named in the subject. **Tag was backfilled 05-23** |
| `v1.6.0` | `c33400f` | 2026-05-10 | **2026-05-23** | `chore: archive v1.6 milestone files` | the milestone's own archive commit. **Tag was backfilled 05-23** |
| `v1.7.0` | `715a5f7` | 2026-05-23 | 2026-05-23 | `fix(logging): preserve per-sink JsonFormatter by setting pattern on sinks` | last functional commit of the logging milestone. Its archive commit is `54f44ae`, whose subject still carries the milestone's earlier name (`chore: archive v1.0 milestone`; renamed to v1.7 in `9ba6f0a`), so the functional close is used instead |
| `v2.0.0-alpha.1` | `8e0a5db` | 2026-08-03 | — | `docs(openspec): sync specs and archive cli-help-tiering-and-logging` | D2 boundary #1 (breaking); archive commit, same day as the change closed |
| `v2.0.0-alpha.2` | `e94d3fc` | 2026-08-19 | — | `feat: consolidate intermediates under hidden .encro work root` | D2 boundary #2 (breaking), taken at its **landing** commit rather than the 08-26 archive (`2090318`), because the incompatible change is in the code that landed 08-19 |
| `v2.0.0-alpha.3` | `54d5be1` | 2026-09-28 | — | `docs: archive the identity-embedding grouping change` | D2 boundaries #3 and #4 (09-26 exit code 130, 09-28 required second model); anchored at the later of the two |
| `v2.0.0-rc.1` | `515d302` | 2026-10-05 | — | `docs: archive the organize-existing-structure change` | D3's release-candidate point |
| `v2.0.0` | `216cc74` | 2026-10-18 | — | `docs: archive the unify-test-task change and sync its specs` | HEAD when the rewrite runs; first public release |

Total: **10 rebuilt + 3 early 0.x + 4 pre-release + 1 normal release = 18 tags**, so `CHANGELOG.md` needs 18 released sections plus `[Unreleased]`.

### Group 2 record

- **2.1 method.** The three 0.x anchors predate OpenSpec, so they were located by feature cluster: the crash-handler work that ends March (`e0dad58`, the month's last commit), the output-layout + zip-name-collision + pack-grouping cluster that ends 04-13 (`aa5fcec`), and the resumable-pack + job-state + e2e + immer-cache cluster that ends 04-19 (`ee2b340`). All three fall in the months the 2026-10-05 exploration predicted.
- **2.2 method.** `v0.1.0` and `v0.1.5` were found by matching their Appendix subjects; both matched exactly and on the recorded dates, which independently confirms that the Appendix's dates are commit dates for those two. `v1.0`–`v1.3` were found by their own milestone markers (`chore: archive vX.Y milestone`), which land on the Appendix date in every case. `v1.4`–`v1.6` close their milestones on 05-01 / 05-05 / 05-10; the Appendix's 05-23 is the day those three tags were created in one backfill pass, proven by `c5d3171 chore: align milestone version — next tag will be v1.7 (project at v1.6)` and `9ba6f0a chore: rename milestone v1.0 -> v1.7`. `v1.7.0`'s archive commit carries the milestone's pre-rename name, so its functional close was used instead.
- **2.3 method.** `alpha.1` is the archive commit of the 08-03 breaking change; `alpha.2` is the **landing** commit of `consolidate-intermediate-files` (08-19), not its 08-26 archive, because the `.encro` work-root move is in the code that landed that day; `alpha.3` is the 09-28 archive commit, the later of the two remaining breaking boundaries; `rc.1` is the 10-05 archive commit; `v2.0.0` is `216cc74`.
- **2.4 checks.** All 18 rows resolve with `git show -s`. Dates are strictly ascending. `git merge-base --is-ancestor` confirms every anchor is an ancestor of the next — the full chain `v0.1.0 → … → v2.0.0` is linear and ordered. Span sizes: largest is `alpha.2 → alpha.3` with 435 commits (six weeks, the cost of keeping only four pre-release points), then `alpha.1 → alpha.2` with 186 and `v1.6.0 → v1.7.0` with 133; the rest are 9–68.

## 3. CHANGELOG content (written before the rewrite; no history change yet)

- [x] 3.1 Draft the per-version sections from the three sources per D6 — archived change proposals in the range, `feat`/`fix` subjects in the range, and for 0.x the subjects alone — as user-facing statements under the six Keep a Changelog types; drop merges, formatting, refactors and docs. Verify: `rg -c '^## \[' CHANGELOG.md` equals the tag count from 2.4, and no entry quotes a commit subject verbatim (`rg` for the raw subjects finds none).
- [x] 3.2 Write the header (`# Changelog`, the Keep a Changelog 1.1.0 and semver 2.0.0 links), the `## [Unreleased]` section, and the trailing link block — compare URLs `vPREV...vCUR` for every version, `releases/tag/v0.1.0`-style for the lowest, `v2.0.0...HEAD` for Unreleased. Verify: every link in the block resolves against `https://github.com/Arteiimis/encro/compare/` or `/releases/tag/` once the tags exist (check with `gh api` after task 6.x, or by format inspection now).
- [x] 3.3 Save one snapshot **file** per anchor under the change's scratch directory — each being the file as it read at that point (that version's section newest, no later version present) — and record the anchor→snapshot path map for the callback. The blobs themselves are created inside the mirror in 5.3, because a blob written here is unreachable there (design D6). Verify: for the earliest and latest anchors, `head -20 <snapshot>` shows the expected newest section; the file count equals the tag count from 2.4.

### Group 3 record

- **3.1 sources and method.** Each span's material came from the archived change names inside it plus that span's `feat`/`fix` subjects (`git log --first-parent --no-merges`), then every entry was rewritten as a statement about what a user can now do. Merges, formatting passes, `refactor`/`style`/`docs`/`chore` commits and test-only work were dropped. `v1.3.0` and `v1.4.0` have no `###` group at all: both milestones were pure internal restructuring (OO migration, PackRequest), which the `changelog-maintenance` spec says must not appear.
- **3.1 checks.** 18 released sections plus `[Unreleased]` = 19 `## [` headings, matching the 18 tags from 2.4. Scanning every `main` commit subject of 25+ characters against the finished file found **zero** verbatim quotes, so no entry is a copied commit log. Dates are strictly descending.
- **3.2 checks.** 19 link entries, one per heading, with no section missing a link and no link without a section. Every released version links `compare/vPREV...vCUR` except the lowest, which links `releases/tag/v0.1.0` because it has no predecessor; `[unreleased]` links `v2.0.0...HEAD`.
- **3.3 mechanism.** `snapshots/CHANGELOG.at-<version>.md` holds each anchor's file: the header, `[Unreleased]`, that version and every earlier section, then a link block cropped to exactly those versions with `[unreleased]` pointing at the tag itself. Sizes grow monotonically from 11 lines (`0.1.0`, one section) to 166 (`2.0.0`, all nineteen), which is the cumulative-snapshot shape the `changelog-maintenance` spec requires. `snapshots/regenerate.ps1` rebuilds all 18 from `CHANGELOG.md`, so an edit to one entry propagates without hand-editing 18 files.
- **3.3 checks.** 18 files, one per tag. For the earliest anchor `CHANGELOG.at-0.1.0.md`, the two `## [` headings are `Unreleased` then `0.1.0`; for the latest, `Unreleased` then `2.0.0` followed by all seventeen earlier versions.

## 4. Version string single source

- [ ] 4.1 Red-first: add a test asserting `encro --version` reports the declared version — the value of the generated `ENCRO_VERSION` macro — with no other version literal in the output. Verify: the test fails against today's binary (`encro v1.6` vs the declared value).
- [ ] 4.2 Set `set_version("2.0.0")` in `xmake.lua:3` (today it reads `0.1.5`), add `src/app/version.h.in` with `${define ENCRO_VERSION}`, wire `set_configvar("ENCRO_VERSION", project.version(), {quote = true})` plus `add_configfiles` in `xmake.lua`, and print the macro at `app_entry.cpp:140` in place of the literal. Verify: 4.1's test passes; `xmake build -r encro` (not a plain build, which can reuse a cached object) and `encro --version` prints `2.0.0`; changing `set_version` to a scratch value and rebuilding changes the printed string with no other source edit; the output carries no stray `v` prefix.

## 5. History rewrite in a mirror

- [ ] 5.1 Save the rollback artefact: `git bundle create <tmp>/pre-rewrite.bundle --all`, and record the pre-rewrite commit count and per-tag tree hashes. Verify: `git bundle verify <tmp>/pre-rewrite.bundle` passes and the recorded count equals `git rev-list --count --all`.
- [ ] 5.2 Mirror-clone to a scratch directory and confirm the mirror's `main` equals origin's. Verify: `git -C <mirror> rev-parse main` equals `git rev-parse origin/main` in the project.
- [ ] 5.3 Materialize the 3.3 snapshot blobs **inside the mirror** (`git -C <mirror> hash-object -w --stdin`) and confirm each is readable there, then run `git filter-repo --commit-callback` in the mirror to inject them at the 2.4 anchors. Verify: `git -C <mirror> cat-file -e <blob>` succeeds for every snapshot before filter-repo starts; the callback reports exactly the expected number of injections; `git -C <mirror> rev-list --count main` equals the pre-rewrite count; for every anchor the mirror tree equals the source tree except for `CHANGELOG.md`.
- [ ] 5.4 Verify the rewrite structurally before any push: every tag name matches `^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-(0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(\.(0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*)?(\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$` — the full semver 2.0.0 grammar: no empty, leading-zero or trailing-dot identifier in any position, build metadata allowed, and each pre-release identifier either a zero-free number or one containing a letter or hyphen (`alpha.01` must fail); one `CHANGELOG.md` per tag whose newest section is that tag's version; `git describe --tags` order matches D4's precedence and ancestry; no tag is an ancestor of a lower one; `git log --first-parent` order unchanged. Verify: all five checks pass on the mirror; a failure stops the change and the project keeps its current refs.
- [ ] 5.5 Push `--mirror --force` to origin, refresh every working tree (`git fetch --all --prune && git reset --hard origin/main`), recreate the tags from the mirror, and delete the now-stale `explore/*` branch. Verify: `git ls-remote --tags origin` lists exactly the 2.4 tag set, `git status` is clean in each working tree, and `encro --version` on a rebuilt binary equals the newest tag's version with both its `v` prefix and any pre-release suffix stripped.

## 6. Stale pointer cleanup and spec sync

- [ ] 6.1 Refresh the commit hashes the rewrite invalidated: only tokens under `openspec/changes/archive`, `docs/backlog.md` and `openspec/specs/cancellation-reporting/spec.md` that resolve to a pre-rewrite commit, rewritten to its new hash — verification per token by `git cat-file -e <old>`/`<new>` before and after, leaving non-hash tokens (exit codes, byte counts, sha256 prefixes, CI run ids) untouched and archived prose unedited. Verify: `rg -o '\b[0-9a-f]{7,40}\b'` before/after differ only in the intended tokens, and every remaining hash-shaped token either resolves or is on the recorded non-hash list.
- [ ] 6.2 Run `openspec validate --archived`. Verify: it reports zero failures; note that archived change documents' own text is untouched, so any failure is a real inconsistency to fix rather than a rewrite artefact.

## 7. Integration verification

- [ ] 7.1 Cross-source agreement: the newest tag's version **without its `v` prefix** (the tag is `v2.0.0`, the bare version `2.0.0`), `encro --version` (fresh release build), the top released `CHANGELOG.md` heading and the anchor table in 2.4 all name the same version. Verify: the four values printed side by side are identical once the tag name's `v` is stripped — the tag is the only one of the four that carries a prefix.
- [ ] 7.2 Full gate on the rewritten tree: `xmake test-report --suite=all` green with every shard executing its assigned count, and `build/windows/x64/release/tests.exe -r console -s` reporting 0 failures. Verify: both pass; the report's assertion total matches the pre-rewrite baseline.

## Planning-artifact review

One independent reviewer, two lenses (Coherence, Ground truth), against the four artifacts; then fresh verifier passes. Every fix landed in the single planning-fix commit that carries this record.

**Round 1 — 11 findings, all resolved:**

- C1 [P0] no task set the release version, so 5.5/7.1 had to fail at 0.1.5 vs 2.0.0 → `set_version("2.0.0")` added to 4.2.
- C2 [P1] tag-inventory size stated three incompatible ways → replaced by `10 rebuilt + 3 early + N pre-release + 1 normal`, N fixed by 2.3.
- C3 [P1] the ten rebuilt tags named two-part while the grammar requirement rejects that, with no task verifying the rename → 2.2 states the mapping, 5.4 verifies it.
- C4 [P1] snapshot blobs hashed in the worktree are unreachable in the mirror → 3.3 saves files, 5.3 hashes inside the mirror.
- C5 [P1] the spec's MAJOR rule contradicted the pre-release tag line, the carve-out living only in design prose → the requirement now carries §8's published-versions scope plus a two-breakings-one-MAJOR scenario.
- C6 [P2] breaking-marker count given as nineteen, eighteen and a sixteen-row table → 14 marker lines across 12 proposals, with the 16 audited rows explained.
- G1 [P1] the verbatim tag messages had no source in the repository → `design.md` Appendix records all ten, noting the two lightweight tags.
- G2 [P1] the "new" `--version` shape already existed → the change is the value, not the shape; no BREAKING marker anywhere.
- G3 [P1] the job-state boundary dated 08-19 while its archive directory reads 08-26 → landing-vs-archive dates explained, anchor takes the chosen commit's own date.
- G4 [P2] the version literal cited `app_entry.cpp:139` (the `if`) → 140 everywhere.
- G5 [P1] the xmake `project.version()` path cited as if in-repo → named as the installed xmake's path.

**Round 2 verify — 6 resolved, 1 not resolved, plus 5 inconsistencies the first fix introduced:**

- N2 [P1] not resolved: `design.md` said anchors are `docs: archive` commits and also "never the directory name", while 2.3 allowed either commit → all three statements now say "archive commit or landing commit, whichever is the functional close point".
- New 1: a stale `18` survived in D1's rationale → 14.
- New 2: D2's summary said "nine other rows" against a table with twelve → named the rows.
- New 3: the tag regex accepted `alpha.01` → per-identifier grammar; verified against 20 samples (accepts `+build.1`, rejects `alpha.01`, `beta.01`, `v1.7`, `V2.0.0`).
- New 4: 5.5 still compared `--version` to the raw tag name → strips the `v` prefix and any pre-release suffix.
- New 5: 2.2 demanded byte-for-byte equality for the two lightweight tags that have no recorded message → the byte check is scoped to the eight annotated messages.
- N1, N3, N4, N5, N6 resolved: Appendix source scoped to 2.2 only; `v`-prefix comparison stated; lightweight-tag wording aligned; build-metadata grammar accepted; `--version` output placed outside the declared API.

**Cap:** the fix→verify loop reached its two-round cap at round 2. The round-2 follow-up fixes above are recorded but were not independently re-verified; the next reader of this change should treat them as unaudited.

## Workflow follow-up

- Run `openspec-verify-change` for this change, then the `code-review` skill with the change's spec path, per AGENTS.md.
- Archive the change once the verify report and review are satisfied, then hand the version numbers to `release-pipeline-and-scoop`.
