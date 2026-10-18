# Design

## Context

See `proposal.md` — Why. The facts that shape the approach:

- Four version sources disagree: `set_version("0.1.5")` (`xmake.lua:3`, stale since March), the literal `"encro v1.6"` (`src/app/app_entry.cpp:139`), ten tags that no longer exist, and zero tags on the remote. `git tag -l` is empty; `git ls-remote --tags origin` is empty.
- 2026-10-08 rewrote all history with `git filter-repo` to strip 334 commit bodies; that pass deleted the ten local tags (they pointed at pre-rewrite commits) and changed 1148 of 1205 hashes. Nobody rebuilt the tags. The change's anchors are therefore unknown — every hash the 2026-10-05 exploration recorded is dead.
- The repository is 1,249 commits, 83 archived OpenSpec changes, no external forks, one user. `main` is at `26aa49d`.
- `git-filter-repo` a40bce5 is installed (`D:\scoop\apps\python\current\Scripts\git-filter-repo.exe`, Python 3.15.0).
- `xmake/core/project/project.lua:1054` exposes `project.version()`, so a `configvar` can read the value `set_version()` declared — the version string can have one source.
- 18 `BREAKING` markers exist across 12 archived changes (full table below).
- Keep a Changelog 1.1.0 rejects commit-log dumps as changelogs; git-cliff output is a draft, never the artifact.

## Goals / Non-Goals

**Goals:**

- One machine-checkable answer to "what version is this?" — the same string in the tag, `encro --version`, `CHANGELOG.md` and (later) the scoop manifest.
- A tag line whose ancestry order and numerical order agree, judged by semver 2.0.0 precedence.
- A `CHANGELOG.md` a reader can use to decide whether to upgrade, and which is present in each tag's own tree.
- A written rule set that makes every future major/minor/patch call mechanical instead of a judgement call.

**Non-Goals:**

- No artifacts, no CI release job, no scoop bucket — the follow-up `release-pipeline-and-scoop` change.
- No rewriting of what archived change documents *say* (commands, semantics, rationale). Only stale commit-hash pointers are refreshed.
- No dependency or build-system change beyond the generated version header.
- No attempt to prove any historical milestone was ever "released"; the pre-release identifiers exist precisely to avoid claiming that.

## Decisions

### D1 — What semver judges: a written public API

`docs/public-api.md` enumerates the surfaces: CLI subcommands and flag names, the value sets each flag accepts, process exit codes, `config.json` keys and value types, the default job-state path and persisted format, and `--log-json` field names. Everything else is explicitly outside: internal C++ interfaces, the test tasks and fixtures, error-message wording, terminal rendering (progress bars, colors), and files a version newly writes.

*Why:* semver 2.0.0 §1 makes a declared API a MUST, and the four breaking calls below are only defensible with a written surface list. *Alternative rejected:* judging breaking-ness per change by feel — that is what produced 18 undifferentiated `BREAKING` markers.

### D2 — Breaking classification of the archive (the evidence for D3)

| # | Change (landed) | Marker content | Verdict |
|---|---|---|---|
| 1 | `cli-help-tiering-and-logging` (08-03) | `-v`/`--verbose` semantics flipped; `-e`/`--verbose-echo` removed; `--flat` removed | **Breaking** — flags and accepted values |
| 2 | `migrate-boost-process-v2` (08-09) | `NewProcessGroup` extension point replaced | Not breaking — e2e helper internals |
| 3 | `test-failure-diagnostics` (08-13) | piped runs stop emitting bar frames | Not breaking — undeclared rendering surface |
| 4 | `consolidate-intermediate-files` (08-19) | default job-state path moves to `.encro\` | **Breaking** — declared path |
| 5 | `cli11-native-validation` (08-23) | error message texts change | Not breaking — accepted/rejected set unchanged |
| 6 | `reduce-over-engineering` (09-04) | test-only pack API deleted | Not breaking — internal |
| 7 | `shard-tests-by-name` (09-18) | shard count fallback removed | Not breaking — dev tooling |
| 8 | `unify-cancel-reporting` (09-26) | cancel exits 130, not 1 | **Breaking** — exit code |
| 9 | `identity-embedding-for-grouping` (09-28) | `--download-models` gains a required second model | **Breaking** — flag's job changes |
| 10 | `clip-normalize-identity-input` (09-29) | identity cache format raised, old cache discarded | Not breaking — cache is rebuildable, no user step |
| 11 | `fuse-tags-and-average-linkage` (09-29) | clustering output differs | Not breaking — new output only, nothing renamed or deleted |
| 12 | `same-run-folder-match` (09-29) | first-run folder set differs | Not breaking — same reasoning as #11 |
| 13 | `organize-existing-structure` (10-05) | proposal states non-breaking | Not breaking |
| 14 | `case-insensitive-media-extensions` (09-28) | proposal states non-breaking | Not breaking |
| 15 | `structured-log-correlation` (08-10) | proposal states non-breaking | Not breaking |
| 16 | `finer-grained-video-resume` (08-02) | design states no breaking | Not breaking |

Four breaking boundaries: 08-03, 08-19, 09-26, 09-28. Each is a real removal, rename, value-set change, exit-code change or required-input change on a declared surface.

### D3 — Unreleased milestones are pre-releases of v2.0.0

Four consecutive MAJOR bumps would produce `v2.0.0 → v5.0.0` for versions nobody ever installed. semver §9 exists for this: a pre-release "indicates that the version is unstable and might not satisfy the intended compatibility requirements". Every milestone after `v1.7.0` was never published, so each becomes a pre-release of the version the first release belongs to:

```
v1.7.0                 05-23   (rebuilt from its original message)
v2.0.0-alpha.1         08-03   cli-help-tiering            ← breaking
v2.0.0-alpha.2..N      08-07…  positional input, structured logging, cross-platform build, …
v2.0.0-alpha.N         08-19   consolidate-intermediates   ← breaking
v2.0.0-beta.1..N       08-30…  probe/preview, persistent config, shell completion, organize v1, …
v2.0.0-beta.N          09-26   unify-cancel-reporting      ← breaking
v2.0.0-beta.N          09-28   identity-embedding          ← breaking
v2.0.0-rc.1            10-05   organize-existing-structure
v2.0.0                 10-18   first public release
```

`alpha` covers the span where breaking changes are still landing; `beta` starts once the declared surfaces stop moving; `rc.1` is the last pre-release before the first publish. Precedence must read `alpha.1 < … < alpha.N < beta.1 < … < beta.N < rc.1 < 2.0.0`, and the claimed anchors must ascend in ancestry order (D4).

*Alternative rejected:* giving each breaking boundary its own MAJOR (`v2.0.0`, `v3.0.0`, `v4.0.0`, `v5.0.0`) — semver-legal but it makes the first scoop release `v5.0.0` and encodes four "releases" that never existed. *Alternative rejected:* collapsing everything since `v1.7.0` into a single `v2.0.0` — loses the milestone narrative the backfill exists for.

Milestone granularity is the archived-change boundaries: one tag per `docs: archive` commit in a contiguous functional cluster, not one per commit. The exact anchor list is task 2.x output, not a design decision — but the count is bounded (≈12–14 tags total) and each must satisfy D4.

### D4 — Anchors are located by archaeology against the rewritten history

Every anchor is `git log --first-parent` output on the **post-filter-repo** history, cross-checked against the archived change's `docs: archive` commit and the change's own prose. No hash recorded before 2026-10-08 is usable, and none will be trusted from a document: each is re-derived and pasted into `tasks.md` with its date and subject before any tag is created. The 0.x anchors (before OpenSpec existed) come from feature clusters only: first `crash_runtime` commit for `v0.2.0`, the output-layout/pack-grouping cluster for `v0.3.0`, the resumable-pack/job-state cluster for `v0.4.0`.

### D5 — Rewrite through a mirror clone, not the working tree

`git filter-repo` rewrites every ref and refuses to run on a dirty or worktree-bearing repository; the current checkout uses a `.git` pointer file (`gitdir: …/worktrees/release-process`). The rewrite therefore runs on a throwaway mirror:

1. `git clone --mirror https://github.com/Arteiimis/encro.git <tmp>/encro-mirror.git`
2. Run `git filter-repo` there with a `--commit-callback` that appends the `CHANGELOG.md` blob for each anchor commit (D6).
3. Verify in the mirror: per-tag tree equality with the source commits, `CHANGELOG.md` present in every tag's tree with the right newest section, commit count preserved, and `git log --first-parent` order intact.
4. Only then `git push --mirror --force` back to origin.
5. Re-point the existing working trees (`git fetch --all --prune && git reset --hard origin/main`), delete the stale `explore/*` branch, and re-create the deleted tags from the mirror.

*Alternative rejected:* `git rebase -i --root` with 19 `edit` stops — `CHANGELOG.md` is a new file whose content differs at every stop, so 18 of the 19 stops conflict. *Alternative rejected:* distributing a `--commit-callback` run inside the project worktree — it would rewrite the refs the worktree and every subagent session depend on, with no verified intermediate copy.

### D6 — CHANGELOG snapshots are precomputed blobs, injected per anchor

Before the rewrite, the 19 sections are written by hand into a single `CHANGELOG.md`, and each anchor's snapshot (the file as it read at that point — newest section first, none of the later ones) is materialized with `git hash-object -w`. The callback then maps `commit.original_id` to a snapshot blob and adds a `FileChange(b'M', b'CHANGELOG.md', blob, b'100644')`; `M` on a path the commit does not yet touch is an addition.

Entry content is curated from three sources per version: the archived change proposals in the range, the `feat`/`fix` subjects in the range, and — for the 0.x era, which predates OpenSpec — the commit subjects alone. `git log --format` output is a draft; every entry is rewritten as a user-facing statement and routine commits (merges, formatting, refactors, docs) are dropped, per the `changelog-maintenance` spec.

*Alternative rejected:* generating with git-cliff and committing the result — Keep a Changelog names commit-log diffs a bad practice, and the historical ranges are full of merge and docs commits.

### D7 — Version string: one declaration, one generated header

`set_version()` stays the single declaration. `xmake.lua` adds `set_configvar("ENCRO_VERSION", project.version(), {quote = true})` and `add_configfiles("src/app/version.h.in")`; the template holds `${define ENCRO_VERSION}`; `app_entry.cpp` prints the macro. `project.version()` is confirmed present at `xmake/core/project/project.lua:1054`, so the configvar reads the same value rather than duplicating a literal.

`--version`'s output becomes `encro <version> (build: <timestamp>)`. This is the change's one BREAKING entry on the binary itself and is recorded as such in the changelog of the version that carries it. *Alternative rejected:* deriving the version from `git describe` at build time — source tarballs and shallow CI clones have no tags, and a build that reports a different version than its source is worse than a stale literal.

### D8 — Stale hash pointers

The rewrite changes every hash from 2026-02-27 onward. Inside `openspec/changes/archive` there are 68 hash-shaped tokens and outside it 2 files / 17 tokens (`docs/backlog.md`, `openspec/specs/cancellation-reporting/spec.md`); some of those are not commit hashes (exit code `1073741510`, byte count `314403447`, sha256 prefixes, a CI run id). The pass rewrites only tokens that resolve to a rewritten commit, verified by a before/after `git cat-file -e` check per token, and it does not touch archived documents' prose — the same rule the 2026-10-08 rewrite applied.

### D9 — One atomic rewrite, gated on a quiet tree

All history-touching work (tag creation, CHANGELOG injection, version-header commit if it must precede an anchor) happens in a single mirror rewrite, because splitting it doubles the hash churn and the D8 cleanup. The gate: `openspec list` shows no active change other than this one, `unify-test-task` has been archived, every working tree is clean, and no subagent session holds a checkpoint ref.

## Risks / Trade-offs

- **Every hash changes again, six days after the last rewrite** → the D8 pass is scoped to tokens that actually resolve, measured before and after; the mirror approach means the working repo stays usable until the push is verified.
- **A wrong anchor puts a milestone tag on the wrong commit** → anchors are re-derived from `--first-parent` archaeology and pasted into `tasks.md` for review before any tag exists; the mirror is verified by tree equality against the pre-rewrite commits.
- **Curated changelog entries are an interpretation, not a record** → every entry is traceable to a named archived change or a `feat` subject in the range, and the design keeps the version→range mapping in `tasks.md`.
- **Pre-release identifiers are unconventional for a private tool** → they are exactly what semver prescribes for never-published milestones, they keep the first public release at `v2.0.0`, and consumers (scoop) only ever see the normal versions.
- **The rewrite cannot be validated by tests** — it is repository metadata → verification is structural: tag count, per-tag `CHANGELOG.md` presence and newest section, precedence order, ancestry order, tree equality of non-CHANGELOG paths.
- **`filter-repo` in a mirror restarts `remote.origin` state** → the mirror is disposable; the push is `--mirror --force` and the fallback is the pre-push bundle saved before step 1.

## Migration Plan

1. Save a bundle of the current refs (`git bundle create <tmp>/pre-rewrite.bundle --all`) — the rollback artefact.
2. Land the version-header change and the full `CHANGELOG.md` on `main` as ordinary commits (they are inputs to the rewrite, and they make the file exist for a reader before the tags do).
3. Run D5 steps 1–4, verify, push `--mirror --force`.
4. Refresh working trees, re-create tags from the mirror, run D8's pointer pass, then `openspec validate --archived`.
5. Rollback: `git fetch <pre-rewrite.bundle> '*:refs/restore/*'`, then push those refs back — the bundle is the only thing needed.

## Open Questions

None that change the specs or the task breakdown. The anchor list itself is deliberately deferred to task 2.x, where it is produced and reviewed against D4 rather than guessed here.
