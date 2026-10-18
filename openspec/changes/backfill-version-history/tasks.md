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

The commit hashes above are the **pre-rewrite** ones, used to decide where each tag belongs. After step 5.5 rewrote history they no longer resolve, and each tag now points at the rewritten equivalent of that commit — so the tag itself, not this column, is the durable record of the anchor. The version column is unaffected and is what group 7's cross-source check reads.

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

- [x] 4.1 Red-first: add a test asserting `encro --version` reports the declared version — the value of the generated `ENCRO_VERSION` macro — with no other version literal in the output. Verify: the test fails against today's binary (`encro v1.6` vs the declared value).
- [x] 4.2 Set `set_version("2.0.0")` in `xmake.lua:3` (today it reads `0.1.5`), add `src/app/version.h.in` with `${define ENCRO_VERSION}`, wire `set_configvar("ENCRO_VERSION", ...)` plus `add_configfiles` in `xmake.lua`, and print the macro at `app_entry.cpp:140` in place of the literal. Verify: 4.1's test passes; `xmake build -r encro` (not a plain build, which can reuse a cached object) and `encro --version` prints `2.0.0`; changing the declaration to a scratch value and rebuilding changes the printed string with no other source edit; the output carries no stray `v` prefix.

### Group 4 record

- **4.1 test.** `tests/app/version_tests.cpp` calls `appentry::run` with `--version`, wraps it in `testutils::captureStdout` with the assertions outside the capture window, and checks that the output contains `encro ` followed by `ENCRO_VERSION` and never contains `encro v`. It needed the generated header to compile, so the template and the xmake wiring landed with it — that is the smallest infrastructure that lets the test exist at all.
- **4.1 red.** First run against the unmodified printer: `CHECK( out.find(expected) != std::string::npos )` failed (`0.1.5` declared, `encro v1.6` printed) and `CHECK( out.find("encro v") == npos )` failed at position 0. 5 assertions, 2 failed.
- **4.2 mechanism.** `src/app/version.h.in` holds `${define ENCRO_VERSION}`; `encro_core` sets `set_configvar("ENCRO_VERSION", encroVersion, {quote = true})` and `add_configfiles("src/app/version.h.in", {prefixdir = "app"})`, generating `build/app/version.h`; `app_entry.cpp` includes `app/version.h` and prints the macro with no `v` in the format string.
- **4.2 deviations from the written task, both forced by the toolchain.** (a) `project.version()` is **not** usable in this xmake (v3.1.2): neither at file level nor inside a target block is `project` defined (`attempt to index a nil value (global 'project')`), so the version is declared once as a Lua local, `local encroVersion = "2.0.0"`, and both `set_version(encroVersion)` and the configvar read it. The literal still appears exactly once and `--version` still traces to that single edit, which is what the requirement asks for. (b) `add_configfiles` did not put its output directory on the include path, so `encro_core` adds `add_includedirs("$(builddir)", {public = true})` explicitly — without it the generated header is unreachable and the build fails at `'app/version.h' file not found`.
- **4.2 checks.** The test passes (5 assertions). `encro --version` prints `encro 2.0.0 (build: …)` with no `v` prefix. Changing the declaration to `9.9.9` and rebuilding printed `encro 9.9.9` with **no other source edit**; changing it back printed `2.0.0` again, and the exe mtime moved (20:40:35 → 20:41:35), so the verdict is not a stale-binary artefact. Full suite after the change: 967 cases, 954 passed, 13 skipped, 0 failed; 17605 assertions, all passed.

## 5. History rewrite in a mirror

- [x] 5.1 Save the rollback artefact: `git bundle create <tmp>/pre-rewrite.bundle --all`, and record the pre-rewrite commit count and per-tag tree hashes. Verify: `git bundle verify <tmp>/pre-rewrite.bundle` passes and the recorded count equals `git rev-list --count --all`.
- [x] 5.2 Mirror-clone to a scratch directory and confirm the mirror's `main` equals origin's. Verify: `git -C <mirror> rev-parse main` equals `git rev-parse origin/main` in the project.
- [x] 5.3 Materialize the 3.3 snapshot blobs **inside the mirror** (`git -C <mirror> hash-object -w --stdin`) and confirm each is readable there, then run `git filter-repo --commit-callback` in the mirror to inject them at the 2.4 anchors. Verify: `git -C <mirror> cat-file -e <blob>` succeeds for every snapshot before filter-repo starts; the callback reports exactly the expected number of injections; `git -C <mirror> rev-list --count main` equals the pre-rewrite count; for every anchor the mirror tree equals the source tree except for `CHANGELOG.md`.
- [x] 5.4 Verify the rewrite structurally before any push: every tag name matches `^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-(0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(\.(0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*)?(\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$` — the full semver 2.0.0 grammar: no empty, leading-zero or trailing-dot identifier in any position, build metadata allowed, and each pre-release identifier either a zero-free number or one containing a letter or hyphen (`alpha.01` must fail); one `CHANGELOG.md` per tag whose newest section is that tag's version; `git describe --tags` order matches D4's precedence and ancestry; no tag is an ancestor of a lower one; `git log --first-parent` order unchanged. Verify: all five checks pass on the mirror; a failure stops the change and the project keeps its current refs.

- [x] 5.5 Push to origin, refresh every working tree (`git fetch --all --prune && git reset --hard origin/main`), recreate the tags from the mirror, and delete the now-stale `explore/*` branch. Verify: `git ls-remote --tags origin` lists exactly the 2.4 tag set, `git status` is clean in each working tree, and `encro --version` on a rebuilt binary equals the newest tag's version with both its `v` prefix and any pre-release suffix stripped.

### Group 5 record

- **5.1.** Bundle of `main` at `%TEMP%\encro-rewrite\pre-rewrite.bundle`, 4.6 MB, `git bundle verify` reports "The bundle records a complete history" and `… is okay`. `git rev-list --count main` = **1262**; `--all` = 1276, the 14 extra commits being the dead `refs/agents` checkpoints that share no ancestor with `main` and are therefore not part of the rollback surface. Per-anchor tree hashes captured to `%TEMP%\encro-rewrite\baseline-trees.txt`. A first bundle was taken before group 4 landed; it was redone after `07710d7` so the rollback point is the real pre-rewrite state.
- **5.2.** `git clone --mirror https://github.com/Arteiimis/encro.git` → mirror `main` = `07710d788d7f` = project `origin/main`, 1262 commits. `git-filter-repo` a40bce548d2c available in the mirror. **Design gap found and handled:** the mirror also carried `refs/pull/1/head` and `refs/pull/2/head` — GitHub's PR refs, which are not writable, so `git push --mirror` would have failed on them. Both were deleted before the rewrite, leaving `refs/heads/main` as the only ref to rewrite and push.
- **5.3.** Every snapshot hashed **inside** the mirror and verified before the rewrite: sizes 654 → 12341 bytes, monotonically increasing with the version. The callback maps `commit.original_id` to its blob and appends `FileChange(b'M', b'CHANGELOG.md', blob, b'100644')`, skipping any commit that already carries the file. `git filter-repo --force --commit-callback …` parsed 1262 commits, finished in 1.56 s, and left `main` at **`5621ecc4e3f2`** with the count still **1262**.
  - A first attempt produced 18 **empty** blobs (`e69de29`, the empty-tree blob) because the script built snapshot paths from the tag name (`CHANGELOG.at-v2.0.0-…`) while the files are named after the bare version (`CHANGELOG.at-2.0.0-…`), and the `cat-file -e` check passed for the empty blob. Adding a `cat-file -s > 0` assertion exposed it; the blobs were regenerated and the rewrite re-run from a fresh mirror.
  - The task asked for the callback's injection count as the check; `git filter-repo` does not report one, so it was replaced by the stronger per-anchor tree comparison in 5.4's first check, which can only pass if all 18 injections happened.
- **5.4.** All five checks pass on the mirror:
  - **grammar:** all 18 tag names match the strict semver pattern (no leading zeros, no empty identifiers).
  - **content:** every tag's `CHANGELOG.md` has `Unreleased` as its first heading and its own version as the second, so no tag carries a later version.
  - **injection:** for each of the 18 anchors, the mirror tree contains `CHANGELOG.md` and every other path is byte-identical to the pre-rewrite tree (full `ls-tree -r` comparison, not a spot check).
  - **precedence:** the declared order `v0.1.0 … v1.7.0 < v2.0.0-alpha.1 < alpha.2 < alpha.3 < rc.1 < 2.0.0` is strictly ascending under semver 2.0.0 precedence, checked with a pre-release comparator that compares dot-separated identifiers numerically-or-lexically and ranks a normal version above any pre-release of the same core. (Two bugs in that comparator were found and fixed by the check itself: it compared only the leading `alpha` token, and the sign was inverted.)
  - **ancestry:** 153 pairwise `merge-base --is-ancestor` checks confirm no later version sits on an earlier commit.
  - **order:** the 1104-commit `--first-parent` sequence matches the pre-rewrite sequence exactly after mapping through `commit-map`.
- **5.5.** Pushed with explicit refspecs, `refs/heads/main:refs/heads/main` plus `refs/tags/*:refs/tags/*`, rather than bare `--mirror`: `--mirror` also tries to delete remote refs the mirror does not have, which would have targeted GitHub's `refs/pull/*`. `origin/main` went `07710d7 → 5621ecc` (both hashes from the first rewrite; the second rewrite superseded them, see the section below), and 18 tags (all annotated) arrived. Locally, `main` was reset to `origin/main` and the pre-rewrite record commit was cherry-picked back as `07a5256` and pushed. `explore/release-process` was deleted after its worktree was detached onto the rewritten history; both working trees now sit on the rewritten commits with nothing uncommitted (the release-process worktree only carries the untracked `build/` directory).
- **5.5 checks.** `git ls-remote --tags origin` yields exactly the 18 names from 2.4, set-identical to the anchor table. `encro --version` after a rebuild prints `encro 2.0.0 (build: …)`, matching the newest tag with its `v` prefix stripped and no pre-release suffix. Tag messages survived the push (spot-checked: the two 0.x tags describe themselves as rebuilt and quote their recorded commit subject; the three 0.2–0.4 tags state that no original message was recorded).

## 6. Stale pointer cleanup and spec sync

- [x] 6.1 Refresh the commit hashes the rewrite invalidated: only tokens under `openspec/changes/archive`, `docs/backlog.md` and `openspec/specs/cancellation-reporting/spec.md` that resolve to a pre-rewrite commit, rewritten to its new hash — verification per token by `git cat-file -e <old>`/`<new>` before and after, leaving non-hash tokens (exit codes, byte counts, sha256 prefixes, CI run ids) untouched and archived prose unedited. Verify: `rg -o '\b[0-9a-f]{7,40}\b'` before/after differ only in the intended tokens, and every remaining hash-shaped token either resolves or is on the recorded non-hash list.
- [x] 6.2 Run `openspec validate --archived`. Verify: it reports zero failures; note that archived change documents' own text is untouched, so any failure is a real inconsistency to fix rather than a rewrite artefact.

### Group 6 record

- **6.1 scope.** 87 unique hash-shaped tokens across `openspec/changes/archive`, `docs/backlog.md` and `openspec/specs/cancellation-reporting/spec.md`. They classify as: **9** that resolve to a commit on the current `main`, **21** plain numbers (exit code `1073741510`, byte count `314403447`, CI run ids such as `33002646675`), and **57** that resolve to nothing at all.
- **6.1 method.** Resolution was done by matching each token as a prefix of a `commit-map` `old` hash, **not** by `git rev-parse`: the local repository still holds pre-rewrite objects through reflog, and after gc a `rev-parse` on a dead token fails for a reason unrelated to whether it was a commit. Plain-digit tokens were excluded before matching so that a byte count cannot accidentally match a hash prefix. **11** tokens mapped to rewritten commits and were replaced with the same-length prefix of their new hash, over 3 files: `2026-10-13-add-batch-subfolder-tasks/tasks.md`, `2026-10-14-batch-single-confirmation/tasks.md`, `2026-10-18-unify-test-task/tasks.md`. Word-boundary anchors kept a 7-character token from matching inside a longer hash.
- **6.1 checks.** No old token survives anywhere in scope, and every replacement token is present in the documents. The 57 dead tokens are **not** introduced by this rewrite: they are references the 2026-10-08 rewrite already orphaned, and none of them appears in `docs/backlog.md`, in the cancellation-reporting spec, or in the three archive files this task edited — so nothing this change touched was left pointing at a hash that no longer exists.
- **6.2.** `openspec validate --archived` → **88 passed, 0 failed**.

## 7. Integration verification

- [x] 7.1 Cross-source agreement: the newest tag's version **without its `v` prefix** (the tag is `v2.0.0`, the bare version `2.0.0`), `encro --version` (fresh release build), the top released `CHANGELOG.md` heading and the anchor table in 2.4 all name the same version. Verify: the four values are identical once the `v` prefix is stripped from the tag name **and from the anchor table's version column** — those two hold tag names, the other two hold bare versions.
- [x] 7.2 Full gate on the rewritten tree: `xmake test-report --suite=all` green with every shard executing its assigned count, and `build/windows/x64/release/tests.exe -r console -s` reporting 0 failures. Verify: both pass; the report's assertion total matches the pre-rewrite baseline.
- [x] 7.3 Commit the structural checks as a reusable script rather than a one-off: `scripts/verify-release-tags.ps1` reads the tags from git and asserts the name grammar (including a negative self-test that `V1.7.0`, `v1.7`, `v2.0.0-alpha.01` and friends are rejected), annotated-ness, precedence order against commit order, each tag's own newest CHANGELOG section, section/date/link coverage, and that only the six change types appear with no empty group. Verify: it exits 0 on HEAD; deleting a `## [x.y.z] - <date>` from `CHANGELOG.md` makes it exit 1.

### Group 7 record

- **7.1.** The four sources agree on `2.0.0`: the newest tag is `v2.0.0`, `encro --version` prints `encro 2.0.0 (build: …)`, the top released `CHANGELOG.md` heading is `## [2.0.0] - 2026-10-18`, and the anchor table's newest row is `v2.0.0`. Stripping the `v` from the two that carry it (the tag name and the anchor table's version column — both hold tag names) leaves four identical values. Separately, all 18 tags have exactly one released heading in `CHANGELOG.md` and no tag lacks one.
  - The task's original wording claimed the tag was "the only one of the four that carries a prefix"; the anchor table's version column carries the same prefix because it records tag names, so the check was rewritten to strip both rather than bending the data.
  - The anchor table's commit column still holds **pre-rewrite** hashes and cannot resolve after 5.5; a note was added there saying the tags, not that column, are the durable record of where each anchor sits.
- **7.2.** `xmake test-report --suite=all` → **unit 17605 assertions in 967/967 cases** and **e2e 935 assertions in 59/59 cases**, both fully green, so every shard ran its assigned count. The unit total matches the pre-rewrite baseline taken in 4.2 (967 cases, 17605 assertions), which is the expected result for a rewrite that changes only repository metadata and adds `CHANGELOG.md`. `tests.exe -r console -s` reports `967 | 954 passed | 13 skipped` with `17605 | 17605 passed` and **zero failures** — the 13 skips are the pre-existing opt-in `[install]`/`[smoke]` and real-model cases, unchanged by this work.

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

## Verification report (`openspec-verify-change`)

### Summary

| Dimension | Status |
|---|---|
| Completeness | 20/20 tasks; 12/12 requirements present in `docs/public-api.md`, `CHANGELOG.md`, `xmake.lua`, `version.h.in`, `app_entry.cpp`, the tag line |
| Correctness | 12/12 requirements mapped; 16/25 scenarios have automated evidence, 8 rely on judgement or a one-off manual check, 1 not applicable |
| Coherence | D1-D9 followed; two implementation deviations found and `design.md` corrected to match reality |

### Issues

**CRITICAL** — none.

**WARNING**

1. Eight scenarios have no automated evidence. Five are judgement calls that cannot be tested — a change being "classified against a written surface", an undeclared surface never silently becoming contract, internal changes not being breaking, a declared-surface change being breaking, and changelog entries reading as prose rather than a commit dump. Three could be automated but are not: "a stale literal cannot survive" (needs an `xmake.lua` edit plus a rebuild, verified by hand in 4.2), "an out-of-order tag is caught" (needs a pre-creation hook, which does not exist yet), "noise stays out" (currently a repo-wide subject scan in 3.1, not a committed check). Recommendation: the first three are inherent to D9's "the rewrite cannot be validated by tests"; the last three belong to the follow-up `release-pipeline-and-scoop` change, which owns the tag-creation step.
2. Two design deviations existed and are now fixed in `design.md` rather than left as prose in `tasks.md`: D5 prescribed `git push --mirror --force` but the push must use explicit refspecs (a bare `--mirror` targets GitHub's unwritable `refs/pull/*`), and D7 prescribed `project.version()` which xmake 3.1.2 does not expose, so a Lua local carries the single declaration. Both are recorded with their reasons.

**SUGGESTION**

1. `scripts/verify-release-tags.ps1` has no entry point — nothing in `README.md`, `AGENTS.md` or an xmake task points at it. It is the only automated evidence for 15 of the scenarios. Recommendation: wire it into an `xmake` task and mention it in the release documentation as part of `release-pipeline-and-scoop`.

### Final assessment

No critical issues. 1 warning, 1 suggestion. Ready for archive (with noted improvements).

## Code review (code-diff stage)

Three reviewers, one per axis (tier L: 2505 changed lines / 30 files, with the 22 generated snapshots reduced to `--stat`). Fixed point `7f32ba6`. Tooling pre-computed: `xmake fmt -k` passed for 245 files; `xmake tidy` not run (a full clang-tidy pass), noted and skipped.

### Standards — 10 findings

| # | Finding | Verdict |
|---|---|---|
| 1 | Three commit subjects are 72–78 chars, AGENTS.md asks for `<72` | **rejected** — cosmetic, and fixing it means another force push of `main` for no behavioural gain |
| 2 | `snapshots/regenerate.ps1` writes `CHANGELOG.at-*.md` into the repo root, not its own directory | **resolved** — paths now derive from `$PSScriptRoot` on both read and write; verified the run leaves no stray file at the root |
| 3 | The script's summary line advertised an `ancestry` check that no code performs, and section banners jumped 3 → 5 | **resolved** — the check is now named "precedence vs commit order" (which is what it does) and the banners run 1–5 |
| 4 | `regenerate.ps1` hardcodes the 18 versions, so a new CHANGELOG section silently yields no snapshot | **resolved** — the list is derived from the file's own headings |
| 5 | Commit types imprecise (`test:` adding a shipped script, `docs:` adding its self-tests) | **rejected** — same reason as #1 |
| 6 | Dead variable `$tick = [char]96` | **resolved** — deleted |
| 7 | `xmake.lua` / `version.h.in` comments name `set_version()` as the single source, but the single source is `local encroVersion` | **resolved** — both comments now name the variable |
| 8 | `design.md` cites `app_entry.cpp:140`, but the added include moved the print to `:141` | **resolved** — cited by branch name instead of a line number, which cannot drift again |
| 9 | Test tag `[app][version]` does not match the sibling suite's `[appentry]` | **resolved** — now `[appentry][version]`, test still passes |
| 10 | Failure message interpolated the literal `## [` because the pattern had no capture group | **resolved** — the section pattern captures the heading |

### Spec — 10 findings

| # | Finding | Verdict |
|---|---|---|
| 1 | `[2.0.0]` logged test-tooling work (outside the declared API, which the spec forbids) and omitted all 14 user-facing fixes from `2026-10-18-fix-audit-findings` | **resolved** — the section was rewritten from that proposal: 9 `### Fixed` entries, 2 `### Changed`, 1 `### Added`, and the test-task line is gone |
| 2 | Three tag messages carry an ASCII dash where the Appendix records U+2014, so the byte-for-byte check in 2.2 does not actually hold | **resolved by the second rewrite** — the three tags were re-created from the Appendix text with a real U+2014; verified present on all three |
| 3 | D2's audit is one row short: `unify-test-task` declares `**BREAKING**: xmake test-parallel no longer exists` with no row | **resolved** — row 17 added (not breaking: dev tooling), counts updated to 15 lines / 13 proposals / 17 rows |
| 4 | `[2.0.0-alpha.2]` re-records the CLI11 migration already listed under `1.6.0` | **resolved** — that line now says only that positional inputs are accepted alongside `-i`/`-I` |
| 5 | `[2.0.0-alpha.3]` re-records the `run_id` correlation that landed inside alpha.2's span | **resolved** — the entry moved to alpha.2 and names the `task_id`/`input` attributes too |
| 6 | `regenerate.ps1` cannot regenerate the snapshots (writes to the wrong directory) | **resolved** — same fix as Standards #2 |
| 7 | The committed snapshots differ from `git show <tag>:CHANGELOG.md` by a blank line | **resolved by the second rewrite** — the corrected snapshots were re-injected and every tag's `CHANGELOG.md` now matches its snapshot in content (a `git diff -w --ignore-blank-lines` between the working file and `v2.0.0:CHANGELOG.md` is empty; only line endings and trailing blank lines differ) |
| 8 | `scripts/verify-release-tags.ps1` is behaviour no task asked for | **rejected as scope creep, recorded instead** — the verify report made it its only SUGGESTION (15 scenarios had no other automated evidence); task 7.3 now records it |
| 9 | One pre-release tag carries two breaking boundaries, which the spec's scenario wording forbade | **resolved** — the scenario now allows a single pre-release tag to carry more than one boundary within the same unpublished MAJOR, which is the decision that cut the line from 15 tags to 4 |
| 10 | D7 says `--version`'s output is outside the declared API, but `docs/public-api.md` described it | **resolved** — `public-api.md` now separates the value (a promise) from the wording and format (rendering, explicitly not covered) |

### Leanness — 3 findings

| # | Finding | Verdict |
|---|---|---|
| 1 | 37 hand-written lines re-implement semver precedence and a bubble sort, in a script whose job is to stop hand-maintained rules from drifting | **resolved** — replaced by PowerShell 7's own `[semver]` type plus `Sort-Object -Stable`; about 35 lines gone, and the reviewer verified the replacement reproduces the same order on this repo's 18 tags |
| 2 | The snapshot generator hardcodes the version list | **resolved** — same fix as Standards #4 |
| 3 | Two dead lines (`$tick`, and a `#### ` guard that can never match) | **resolved** — both deleted |

Net: **20 resolved in the first pass; the 2 open findings were closed by a second rewrite (below); 2 rejected with reasons; 1 rejected-as-scope-creep recorded as task 7.3.**

## Second rewrite (closing the two open findings)

The first rewrite injected the snapshots that existed before the review, so after the CHANGELOG corrections the `v2.0.0` tag still carried the pre-fix section, and three tag messages carried an ASCII dash instead of the Appendix's U+2014. Both live in repository metadata, which only a rewrite can change — and the window closes the moment anything is published, so it was taken now.

- **Callback change.** The first callback skipped any commit that already carried `CHANGELOG.md`. After the first rewrite the `v2.0.0` anchor *does* carry one (the stale one), so that skip would have preserved the very content being corrected. The second callback instead **replaces** the entry: it drops any existing `CHANGELOG.md` file-change and appends `FileChange(b'M', …)`. Commits after the last anchor that themselves carried the file get `HEAD`'s version rather than the stale one baked in earlier.
- **Result.** 1269 commits in, 1269 out; `main` moved to the rewritten equivalent of `97e159c`; 18 tags re-created, the three recorded messages now carrying a real U+2014.
- **Verification before the push.** For all 18 anchors: `CHANGELOG.md` present, its newest released section equal to the tag's version, and every other path byte-identical to the pre-rewrite tree (both sides filtered for `CHANGELOG.md`, since the first rewrite had already added one). Grammar, annotated-ness and precedence-vs-commit-order all hold. Only then was the push made.
- **After the push.** `scripts/verify-release-tags.ps1` exits 0 on the refreshed working tree; `v2.0.0:CHANGELOG.md` no longer contains the test-task line and does contain the probe-cache fix; both working trees sit on the rewritten `main` with nothing uncommitted.
- **Rollback artefact** for this pass: `%TEMP%\encro-rewrite2\pre-rewrite2.bundle`, the `97e159c` state.

## Workflow follow-up

- Run `openspec-verify-change` for this change, then the `code-review` skill with the change's spec path, per AGENTS.md.
- Archive the change once the verify report and review are satisfied, then hand the version numbers to `release-pipeline-and-scoop`.
