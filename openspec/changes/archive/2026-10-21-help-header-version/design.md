# Design

## Context

See `proposal.md` - Why. The constraints that shape the approach were measured, not assumed (a throwaway xmake probe project outside this repository, then deleted):

- xmake 3.1.2 substitutes `${GIT_COMMIT}` (built from `git rev-parse --short HEAD`), `${GIT_TAG_LONG}` (`git describe --tags --long`) and four more git variables into `add_configfiles` templates, quoting string values; a repository with a tag yields `"2a4e538"` and `"v1.2.3-0-g2a4e538"`.
- Without a regeneration trigger, the generated header keeps the commit it saw at configure time: after a new commit, `xmake build` still reported the old hash.
- `set_policy("build.always_update_configfiles", true)` makes the next `xmake build` regenerate the header to the current commit with no `xmake f`.
- With no `.git` present, every git variable renders the literal string `"none"`, and the build succeeds.
- xmake invokes all six git builtin variables per configfile regeneration regardless of which ones a template references (`actions/config/configfiles.lua`), and copies the result only when the content differs. Measured here: one `git` spawn ~84 ms, so ~0.3-0.5 s added to every build; a no-op `xmake build encro` is 1.1 s.
- The sandbox has `try` but not `pcall`, so a hand-rolled git probe in `xmake.lua` must use `try`.
- `.github/workflows/ci.yml` checks out at the default depth with no tags; `release.yml` uses `fetch-depth: 0`.

## Goals / Non-Goals

**Goals:**

- One release token, composed in one place, printed identically by `-h` / `-hh` and `--version`.
- The token is accurate for the tree it was built from, including a local rebuild after a new commit.
- A build with no git metadata still produces a correct, unstamped token.

**Non-Goals:**

- Making builds bit-reproducible (dropping `__DATE__`/`__TIME__` removes one obstacle, nothing more).
- Changing how the version is declared or when it is bumped.
- A `-dirty` marker, a branch name, or any further build field.

## Decisions

**D1 - Token shape: `v<declared version>` plus `+<short commit>` when available.** `v2.0.3+a5f6f41c`. The `+` form is semver build metadata: legal on a versioned string, ignored by precedence, and it keeps the version value readable by taking everything before `+`. *Alternative rejected:* `v2.0.3-37-ga5f6f41c` (git describe) carries the distance from the tag, but `git describe --tags` needs tag objects, so `ci.yml`'s tag-less shallow checkout would render `none` on every ordinary build, and a release build would read `v2.0.3-0-g...`.

**D2 - Commit source: xmake's builtin `${GIT_COMMIT}`, not a hand-rolled variable.** The builtin degrades to `"none"` when git is missing or the tree is not a repository, so `xmake.lua` needs no error handling. *Alternative rejected:* `set_configvar("ENCRO_COMMIT", try { ... os.iorunv ... } or "", {quote = true})` - one git call instead of six at configure, but a configvar is a configure-time value, so it could never refresh on rebuild (D3). The cost of the builtin is the constant `"none"` sentinel, handled once in C++.

**D3 - Freshness: enable `build.always_update_configfiles`.** Without it the banner lies after the first commit (measured), and a banner that reports the wrong commit is worse than no banner. The price is six git invocations per build (`copy_if_different` keeps the header untouched when the commit has not moved, so nothing recompiles needlessly). *Alternative rejected:* leave the policy off and document `xmake f` after committing - a trap waiting for the first person who checks the banner after a commit.

**D4 - The `v` prefix applies to both surfaces.** `-h` and `--version` must read as one token, and `v` is the release tag's own spelling (`v2.0.3`) while the changelog headings and the Scoop manifest carry the bare form (`2.0.3`) - so the banner becomes the one place the tag's spelling appears. This reverses the no-`v` decision recorded in `backfill-version-history`, justified here because the two surfaces now share one composition site. `docs/public-api.md` puts the wording and format of `--version` outside the promise, so no version increment is required and the public API document needs no edit. The `encro v`-absent assertion in `tests/app/version_tests.cpp` flips with it.

**D5 - Delete `compileTimestamp()` together with `parseDecimal()` and `monthNumber()`.** No surface prints a build time after this change, and those helpers exist only for it. Removing `__DATE__`/`__TIME__` from the binary also removes the reason two builds of identical sources differ.

**D6 - Composition lives in `app_entry.cpp`, not in the generated header.** The header exposes plain `ENCRO_VERSION` and `ENCRO_GIT_COMMIT` strings; one helper turns them into the token (`v` + version, plus `+` + commit unless the commit is empty or `"none"`), and both `helpIntroLine()` and the `--version` branch call it. It is declared in `app_entry.h` beside `helpIntroLine()` rather than kept file-local, because the no-commit cases (empty string, `"none"`) exist only inside a git-less build and a test that takes (version, commit) as arguments is the only cheap way to pin them. Deciding "is there a usable commit" is a string test, and the generated header is a mechanism for getting values in, not a place to grow logic.

## Risks / Trade-offs

- [The header line changes shape, so tests that pinned it break] -> They are rewritten deliberately in the same change; the e2e assertion keeps matching the unchanged `encro: Universal video encoder/converter/packer` prefix.
- [Six git spawns on every build, including no-op builds] -> Accepted at the measured 0.3-0.5 s; if it ever hurts, dropping the policy reverts to a stale hash, not to a wrong report of the current one.
- [Source tarball or git-less `PATH` produces no commit] -> Specified: the suffix is omitted, the version alone is printed.
- [A dirty tree reports its last commit, not the edits] -> Accepted: the commit names the base the binary was built from; a `-dirty` marker was considered and rejected as noise that costs another git call.
- [`"none"` is an xmake implementation detail that could change] -> Compared against in one place; the no-commit spec scenario is what would go red if xmake ever switched to an empty string or an error.

## Migration Plan

Nothing to migrate: no persisted data, no flag, no exit code. Already-released 2.0.3 binaries keep the old banner; the next release carries the new one, and this change raises no version increment of its own.
