# Tasks

## 1. Gate and public API declaration

- [ ] 1.1 Write `docs/public-api.md`: the CLI subcommands and flag names with their accepted value sets, process exit codes, `config.json` keys and value types, the default `job-state.json` path and its persisted format, and the `--log-json` field names; state explicitly what is *not* judged (internal interfaces, test tasks, error wording, terminal rendering, newly written output files). Verify: every surface listed exists in the tree today — `encro -hh` and `encro config list` output, `src/cmd/config_store.cpp`'s key table, and the NDJSON field names in the logging code each match a line of the document; `rg` finds no listed flag absent from `src/cmd/`.
- [ ] 1.2 Close the gate: `openspec list` shows no change other than this one, `unify-test-task` is archived, all three working trees are clean, and `git for-each-ref refs/agents` reports no live checkpoint refs. Verify: the four commands' output is recorded in this task's completion note; if any fails, stop and report rather than proceeding.

## 2. Anchor archaeology (produces the tag list; no tags yet)

- [ ] 2.1 Locate the three 0.x anchors on the post-2026-10-08 history by feature cluster only — `v0.2.0` at the first `crash_runtime` commit, `v0.3.0` at the output-layout / zip-name-collision / pack-grouping cluster, `v0.4.0` at the resumable-pack + job-state + e2e cluster. Verify: each anchor's `git show --stat` contains the named subsystem and its date matches the 2026-10-05 exploration's month (`~03/05`, `~04/13`, `~04/19`); the three form an ancestry chain.
- [ ] 2.2 Locate the ten original tags (`v0.1.0`, `v0.1.5`, `v1.0`–`v1.7`) and restore each one's annotated message verbatim from the 2026-10-05 session record (`v1.5 Pack Refinement — …3033 assertions, 244 tests`, `v1.7 Logging System Optimization — …26 requirements, 340 tests`, …); the messages are recovered text, not re-authored. Verify: each anchor's date matches the recorded tag date (2026-02-27, 2026-03-02, 2026-04-26…05-23) and each message is byte-identical to the record.
- [ ] 2.3 Locate the post-`v1.7.0` milestones at archived-change boundaries and assign the D3 pre-release identifiers — `alpha.N` up to and including `consolidate-intermediate-files` (08-19), `beta.N` from `persistent-user-config`/`shell-completion` (08-30) through `identity-embedding-for-grouping` (09-28), `rc.1` at `organize-existing-structure` (10-05), `v2.0.0` at HEAD. Verify: the resulting list satisfies D4 — date, ancestry and precedence all ascend together — and each anchor is a `docs: archive` commit or a named cluster head, not an arbitrary commit.
- [ ] 2.4 Paste the complete anchor table (version, commit, date, subject, source of the decision) into this file as a review block, and confirm the total tag count is bounded (≈12–14 new plus the ten rebuilt). Verify: every row resolves with `git show -s`, and no row's commit is an ancestor of the row above it.

## 3. CHANGELOG content (written before the rewrite; no history change yet)

- [ ] 3.1 Draft the per-version sections from the three sources per D6 — archived change proposals in the range, `feat`/`fix` subjects in the range, and for 0.x the subjects alone — as user-facing statements under the six Keep a Changelog types; drop merges, formatting, refactors and docs. Verify: `rg -c '^## \[' CHANGELOG.md` equals the tag count from 2.4, and no entry quotes a commit subject verbatim (`rg` for the raw subjects finds none).
- [ ] 3.2 Write the header (`# Changelog`, the Keep a Changelog 1.1.0 and semver 2.0.0 links), the `## [Unreleased]` section, and the trailing link block — compare URLs `vPREV...vCUR` for every version, `releases/tag/v0.0.1`-style for the lowest, `v2.0.0...HEAD` for Unreleased. Verify: every link in the block resolves against `https://github.com/Arteiimis/encro/compare/` or `/releases/tag/` once the tags exist (check with `gh api` after task 6.x, or by format inspection now).
- [ ] 3.3 Materialize one snapshot blob per anchor with `git hash-object -w` — each snapshot being the file as it read at that point (that version's section newest, no later version present) — and record the anchor→blob map in the callback script's data. Verify: `git cat-file -p <blob> | head -20` for the earliest and latest anchors shows the expected newest section, and the blob set has one entry per tag.

## 4. Version string single source

- [ ] 4.1 Red-first: add a test asserting `encro --version` reports the declared version — the value of the generated `ENCRO_VERSION` macro — with no other version literal in the output. Verify: the test fails against today's binary (`encro v1.6` vs the declared value).
- [ ] 4.2 Add `src/app/version.h.in` with `${define ENCRO_VERSION}`, wire `set_configvar("ENCRO_VERSION", project.version(), {quote = true})` plus `add_configfiles` in `xmake.lua`, and print the macro in `app_entry.cpp:139`. Verify: 4.1's test passes; `xmake build -r encro` (not a plain build, which can reuse a cached object) and `encro --version` prints the value from `xmake.lua:3`; changing `set_version` to a scratch value and rebuilding changes the printed string with no other source edit.

## 5. History rewrite in a mirror

- [ ] 5.1 Save the rollback artefact: `git bundle create <tmp>/pre-rewrite.bundle --all`, and record the pre-rewrite commit count and per-tag tree hashes. Verify: `git bundle verify <tmp>/pre-rewrite.bundle` passes and the recorded count equals `git rev-list --count --all`.
- [ ] 5.2 Mirror-clone to a scratch directory and confirm the mirror's `main` equals origin's. Verify: `git -C <mirror> rev-parse main` equals `git rev-parse origin/main` in the project.
- [ ] 5.3 Run `git filter-repo --commit-callback` in the mirror to inject the D6 snapshots at the 2.4 anchors. Verify: the callback reports exactly the expected number of injections; `git -C <mirror> rev-list --count main` equals the pre-rewrite count; for every anchor the mirror tree equals the source tree except for `CHANGELOG.md`.
- [ ] 5.4 Verify the rewrite structurally before any push: one `CHANGELOG.md` per tag whose newest section is that tag's version; `git describe --tags` order matches D4's precedence and ancestry; no tag is an ancestor of a lower one; `git log --first-parent` order unchanged. Verify: all four checks pass on the mirror; a failure stops the change and the project keeps its current refs.
- [ ] 5.5 Push `--mirror --force` to origin, refresh every working tree (`git fetch --all --prune && git reset --hard origin/main`), recreate the tags from the mirror, and delete the now-stale `explore/*` branch. Verify: `git ls-remote --tags origin` lists exactly the 2.4 tag set, `git status` is clean in each working tree, and `encro --version` on a rebuilt binary matches the newest tag without the pre-release suffix.

## 6. Stale pointer cleanup and spec sync

- [ ] 6.1 Refresh the commit hashes the rewrite invalidated: only tokens under `openspec/changes/archive`, `docs/backlog.md` and `openspec/specs/cancellation-reporting/spec.md` that resolve to a pre-rewrite commit, rewritten to its new hash — verification per token by `git cat-file -e <old>`/`<new>` before and after, leaving non-hash tokens (exit codes, byte counts, sha256 prefixes, CI run ids) untouched and archived prose unedited. Verify: `rg -o '\b[0-9a-f]{7,40}\b'` before/after differ only in the intended tokens, and every remaining hash-shaped token either resolves or is on the recorded non-hash list.
- [ ] 6.2 Run `openspec validate --archived`. Verify: it reports zero failures; note that archived change documents' own text is untouched, so any failure is a real inconsistency to fix rather than a rewrite artefact.

## 7. Integration verification

- [ ] 7.1 Cross-source agreement: the newest tag, `encro --version` (fresh release build), the top released `CHANGELOG.md` section and the anchor table in 2.4 all name the same version. Verify: the four values printed side by side are identical.
- [ ] 7.2 Full gate on the rewritten tree: `xmake test-report --suite=all` green with every shard executing its assigned count, and `build/windows/x64/release/tests.exe -r console -s` reporting 0 failures. Verify: both pass; the report's assertion total matches the pre-rewrite baseline.

## Workflow follow-up

- Run `openspec-verify-change` for this change, then the `code-review` skill with the change's spec path, per AGENTS.md.
- Archive the change once the verify report and review are satisfied, then hand the version numbers to `release-pipeline-and-scoop`.
