# Proposal

## Why

encro carries four mutually contradictory version sources — `set_version("0.1.5")` in `xmake.lua:3` (stale since March), a hard-coded `"encro v1.6"` in `src/app/app_entry.cpp:139`, ten local tags that no longer exist, and zero tags on the remote — so `--version` names a release that never was, and no tag, changelog or package manifest can be trusted to agree with it. The project is about to publish Windows and Linux artifacts into a personal scoop bucket, where the manifest's `version` field, the GitHub release tag and `encro --version` are read by humans and tooling alike; that step needs one source of truth before it can exist.

## What Changes

- **Declare the public API.** Add `docs/public-api.md` naming what semver judges: CLI subcommands, flag names and accepted value sets, exit codes, `config.json` keys and value types, the default `job-state.json` path and its format, and the `--log-json` NDJSON field names. Semver 2.0.0 §1 makes this a MUST, and every breaking-change call below depends on it.
- **Adjudicate every historical breaking change.** Nineteen `BREAKING` markers across twelve archived changes are classified against that declaration — four are user-visible (2026-08-03 `-v`/`-e`/`--flat`, 2026-08-19 job-state path, 2026-09-26 cancel exit code 130, 2026-09-28 required second model), the rest are internal API, dev tooling, error-message wording or organize output that no contract covers. The full table lands in `design.md`.
- **Rebuild the tag line.** Re-create the ten historical tags as annotated `vX.Y.Z` with their original messages restored verbatim, add the three early 0.x milestone tags, and carry the eight months after `v1.7.0` as semver 2.0.0 §9 pre-releases (`v2.0.0-alpha.N` / `v2.0.0-beta.N`, one tag per milestone, ordered by precedence) ending at `v2.0.0` for the first public release. **The ten local tags and every commit hash change**: this rewrites history, the second such rewrite after 2026-10-08.
- **Write `CHANGELOG.md`** to Keep a Changelog 1.1.0 — `## [Unreleased]`, one `## [x.y.z] - YYYY-MM-DD` section per tag with `### Added`/`Changed`/`Deprecated`/`Removed`/`Fixed`/`Security` groups, `[YANKED]` support, newest first, and a compare-URL link block — with human-written entries, not a commit-log dump, which the format explicitly rejects.
- **Give every historical tag its own changelog snapshot.** Each rebuilt tag's tree carries the `CHANGELOG.md` as it read at that point, so checking out a tag shows its own entry. This is the reason the rewrite is done with `git filter-repo --commit-callback` rather than a rebase: 19 insertion points, and an interactive rebase would conflict at 18 of them.
- **Make the version string single-source.** `set_version()` feeds an xmake `configvar` that renders a generated header; `app_entry.cpp` prints that macro instead of the literal. **BREAKING**: `--version` output changes shape (it currently reports a version no tag ever carried).
- **Repair stale hash pointers.** Archived change documents hold commit hashes that the rewrite invalidates; update them in one pass, the same way the 2026-10-08 rewrite did.

## Capabilities

### New Capabilities

- `version-numbering`: how encro names versions — the declared public API that semver judges, the tag format and grammar, pre-release usage for unreleased milestones, precedence ordering, and the single source the binary's version string comes from.
- `changelog-maintenance`: what `CHANGELOG.md` must contain — Keep a Changelog 1.1.0 section shape and change types, one entry per version with its date, newest first, the `[Unreleased]` and `[YANKED]` markers, link references, and the rule that a tag's tree carries its own version's section.

### Modified Capabilities

None. `cli-help-layout` and `logging-behavior` describe the surfaces the version string appears on, not its value; no existing requirement changes.

## Impact

- **New files**: `docs/public-api.md`, `CHANGELOG.md`, `version.h.in` (or the generated-header template), and the change's own artifacts.
- **Code**: `xmake.lua` (configvar + generated header), `src/app/app_entry.cpp` (`--version` output), one new version header under `src/`.
- **History**: all 1,249 commits from 2026-02-27 onward are rewritten, so every commit hash changes, `main` needs a force push, and the archive's commit-hash pointers must be refreshed. The repository has no external forks and no other consumers.
- **Tooling**: `git filter-repo` (already used on 2026-10-08), `git tag -a`, and a validation pass over `openspec validate --archived`.
- **Not in this change**: build artifacts, CI release jobs and the scoop bucket — those are the follow-up `release-pipeline-and-scoop` change, which consumes the version numbers this one establishes.
