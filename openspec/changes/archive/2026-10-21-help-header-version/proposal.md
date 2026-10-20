# Proposal

## Why

`encro -h` opens with `encro: Universal video encoder/converter/packer | build: 2026-10-21 00:28:14`, so the one place every user looks first names no release at all. The value it shows instead is not even a trustworthy build stamp: `compileTimestamp()` reads `__DATE__`/`__TIME__`, which is the compile time of `app_entry.cpp` alone, survives an incremental build as a stale value, and makes the binary byte-different on every rebuild of that translation unit. Meanwhile `encro --version` does print the declared version, buried behind a flag most users never run.

The result is that a bug report cannot name the release from the help output, and the project's own release checks (`.agents/skills/release/SKILL.md`) have to tell people to run a second command to learn what `-h` should have said.

## What Changes

- The main-help header (`-h` and `-hh`, i.e. the CLI11 app description line) reports the release instead of a build time: `encro: Universal video encoder/converter/packer | v2.0.3+a5f6f41c`.
- `encro --version` reports the same token and loses its build-time clause: `encro v2.0.3+a5f6f41c`.
- The token carries the `v` prefix the release tag already uses (the changelog heading and the Scoop manifest keep the bare form), plus the `+<short commit>` semver build-metadata suffix when the build knows its commit. Both surfaces compose it through one helper, so they cannot drift.
- The build learns the commit from git at build time, and rebuilds refresh it; a build without git metadata (source tarball, no `git` on `PATH`) omits the `+commit` suffix and keeps the plain `v<version>`.
- `compileTimestamp()` and its `__DATE__`/`__TIME__` parsing helpers are deleted.
- Version behaviour otherwise unchanged: the declared version in `xmake.lua` stays the single source of truth, and no version increment is required, because `docs/public-api.md` places the wording and format of `--version` outside the declared API.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `cli-help-layout`: gains a requirement for the main-help header, which no requirement covers today - it names the declared version and the build commit, and carries no build timestamp.
- `version-numbering`: the `--version` reporting requirement gains the `v` prefix and the git commit as build metadata, and states that the commit is omitted when the build has no git metadata.

## Impact

- `xmake.lua`: enable `build.always_update_configfiles` and generate the commit alongside `ENCRO_VERSION`.
- `src/app/version.h.in`: new generated macro for the short commit.
- `src/app/app_entry.h` / `src/app/app_entry.cpp`: a `versionToken()` helper declared beside `helpIntroLine()`, shared by the header line and the `--version` branch; `compileTimestamp()` and its helpers removed.
- `tests/app/app_entry_tests.cpp`, `tests/app/version_tests.cpp`: the header assertion becomes a version-token assertion, and the `encro v`-absent assertion flips.
- `.agents/skills/release/SKILL.md`: the two manual checks that read `encro --version` expect the new token.
- Build cost: the always-update policy evaluates xmake's git builtin variables on every build (six `git` invocations, measured at 0.3-0.5 s against a 1.1 s no-op build). No new dependency; no CI workflow change.
- Not affected: `docs/public-api.md`, `scripts/windows_package.py` (it only reads the exit code of `--version`), the release pipeline's version/tag agreement check (it reads `xmake.lua`, not the binary).
