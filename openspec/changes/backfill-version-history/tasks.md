# Tasks

## 1. Gate and public API declaration

- [ ] 1.1 Write `docs/public-api.md`: the CLI subcommands and flag names with their accepted value sets, process exit codes, `config.json` keys and value types, the default `job-state.json` path and its persisted format, and the `--log-json` field names; state explicitly what is *not* judged (internal interfaces, test tasks, error wording, terminal rendering, newly written output files). Verify: every surface listed exists in the tree today — `encro -hh` and `encro config list` output, `src/cmd/config_store.cpp`'s key table, and the NDJSON field names in the logging code each match a line of the document; `rg` finds no listed flag absent from `src/cmd/`.
- [ ] 1.2 Close the gate: `openspec list` shows no change other than this one, `unify-test-task` is archived, all three working trees are clean, and `git for-each-ref refs/agents` reports no live checkpoint refs. Verify: the four commands' output is recorded in this task's completion note; if any fails, stop and report rather than proceeding.

## 2. Anchor archaeology (produces the tag list; no tags yet)

- [ ] 2.1 Locate the three 0.x anchors on the post-2026-10-08 history by feature cluster only — `v0.2.0` at the first `crash_runtime` commit, `v0.3.0` at the output-layout / zip-name-collision / pack-grouping cluster, `v0.4.0` at the resumable-pack + job-state + e2e cluster. Verify: each anchor's `git show --stat` contains the named subsystem and its date matches the 2026-10-05 exploration's month (`~03/05`, `~04/13`, `~04/19`); the three form an ancestry chain.
- [ ] 2.2 Locate the ten original tags and restore each one's recorded message verbatim from `design.md`'s Appendix — recovered text, not re-authored, and no longer than the recorded first line. Their recorded names are two-part (`v1.0`…`v1.7`) and the rebuilt tags must be three-part (`v1.0.0`…`v1.7.0`); `v0.1.0` and `v0.1.5` were lightweight tags, so their message is newly written and marked as such, not recovered. Verify: each anchor's date matches the Appendix (2026-02-27, 2026-03-02, 2026-04-26…05-23); the eight annotated messages equal their Appendix line byte for byte; and the two 0.x messages are recorded as newly written rather than compared against the Appendix's "no tag message" line.
- [ ] 2.3 Locate the post-`v1.7.0` milestones at functional close points and assign the D3 pre-release identifiers — `alpha.N` up to and including `consolidate-intermediate-files`, `beta.N` from `persistent-user-config`/`shell-completion` (08-30) through `identity-embedding-for-grouping` (09-28), `rc.1` at `organize-existing-structure` (10-05), `v2.0.0` at HEAD. Each anchor is either the change's `docs: archive` commit or, when the feature actually closed earlier, the landing commit — record which of the two it is and why, since they differ by up to a week (`consolidate-intermediate-files`: landing `e94d3fc` 08-19, archive 08-26). Verify: the list satisfies D4 using **the chosen commit's own date** — date, ancestry and precedence ascend together — and no anchor is an arbitrary commit inside a range.
- [ ] 2.4 Paste the complete anchor table (version, commit, date, subject, source of the decision) into this file as a review block, and state the total as `10 rebuilt + 3 early 0.x + N pre-release + 1 normal release`, with N fixed by 2.3. Verify: every row resolves with `git show -s`, no row's commit is an ancestor of the row above it, and the changelog section count planned in group 3 equals the tag count this table states.

## 3. CHANGELOG content (written before the rewrite; no history change yet)

- [ ] 3.1 Draft the per-version sections from the three sources per D6 — archived change proposals in the range, `feat`/`fix` subjects in the range, and for 0.x the subjects alone — as user-facing statements under the six Keep a Changelog types; drop merges, formatting, refactors and docs. Verify: `rg -c '^## \[' CHANGELOG.md` equals the tag count from 2.4, and no entry quotes a commit subject verbatim (`rg` for the raw subjects finds none).
- [ ] 3.2 Write the header (`# Changelog`, the Keep a Changelog 1.1.0 and semver 2.0.0 links), the `## [Unreleased]` section, and the trailing link block — compare URLs `vPREV...vCUR` for every version, `releases/tag/v0.0.1`-style for the lowest, `v2.0.0...HEAD` for Unreleased. Verify: every link in the block resolves against `https://github.com/Arteiimis/encro/compare/` or `/releases/tag/` once the tags exist (check with `gh api` after task 6.x, or by format inspection now).
- [ ] 3.3 Save one snapshot **file** per anchor under the change's scratch directory — each being the file as it read at that point (that version's section newest, no later version present) — and record the anchor→snapshot path map for the callback. The blobs themselves are created inside the mirror in 5.3, because a blob written here is unreachable there (design D6). Verify: for the earliest and latest anchors, `head -20 <snapshot>` shows the expected newest section; the file count equals the tag count from 2.4.

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
