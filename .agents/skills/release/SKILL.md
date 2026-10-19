---
name: release
description: Cut an encro release, or repair one — bump the version, write the changelog section, tag, and drive the publishing workflow; also diagnose a failed release run and a broken Scoop install. Use whenever the user says 发版 / 发个版本 / 发版失败 / cut a release / publish vX.Y.Z / bump the version, or when a release run failed, a published artifact is missing or wrong, or `scoop install` / `scoop update encro` fails.
---

# Cutting a Release

## Where the contract lives

Read these for the contract; use this skill for the procedure.

- `openspec/specs/release-pipeline/spec.md`, `scoop-distribution`, `version-numbering`, `changelog-maintenance` — the requirements and scenarios a release must satisfy.
- `docs/public-api.md` — the surfaces semver judges, and which increment a change earns.
- `.github/workflows/release.yml` — the mechanism. Its header comment lists the four entry points.

## Procedure

### 1. Pre-flight, in the working tree

```powershell
xmake build encro encro_e2e_tool tests e2e_tests
xmake test-report --suite=all                         # AGENTS.md's single entry point
pwsh scripts/verify-release-tags.ps1                  # tag grammar, order, per-tag changelog section
pwsh scripts/verify-changelog-sections.ps1            # the text that becomes release bodies
```

`main` must equal `origin/main` and the tree must be clean. The bar is zero failures, not a case count — the suite grows every time a case is added, so a run that reports more cases than you remember is normal; the 13 skips are the pre-existing opt-in `[install]` / `[smoke]` / `[real-model]` cases.

CI's gate runs the two test binaries directly instead of `test-report`: on a fresh runner the wrapper enumerated fewer cases than `ci.yml` reported, split them into two shards from an empty cost model, and aborted eight seconds later without naming a single assertion. `test-report` is the local entry point; the gate does what `ci.yml` does.

### 2. Write the changelog section

A new `## [X.Y.Z] - YYYY-MM-DD` directly under `## [Unreleased]`, newest first. Group entries under `Added` / `Changed` / `Deprecated` / `Removed` / `Fixed` / `Security` — no empty group, no commit-subject narration, no internal refactor. Then add the link and re-point unreleased:

```markdown
[unreleased]: https://github.com/Arteiimis/encro/compare/vX.Y.Z...HEAD
[X.Y.Z]: https://github.com/Arteiimis/encro/compare/vPREV...vX.Y.Z
```

Two releases may share a date; dates must not increase down the file.

### 3. Bump the one declaration

`local encroVersion = "X.Y.Z"` at the top of `xmake.lua`. That single literal feeds `set_version` and the generated `app/version.h`, so `encro --version`, the artifact name and the tag agree. Nothing else carries the version — the declaration is the source of the gate that refuses a tag naming a version the build does not stamp.

### 4. Commit, push, tag

```powershell
git commit -m "chore: release X.Y.Z"
git push origin main
git tag -a vX.Y.Z -m "<one line: what this release is>"
git push origin vX.Y.Z
```

The tag message is not the release body — the body is cut from the changelog section. Two pushes, not one: `git push --follow-tags` on a branch with no new commits pushes nothing.

Done when `git ls-remote --tags origin vX.Y.Z` prints the tag: at that moment the workflow is running against the commit the tag names.

### 5. Watch the run

```powershell
$id = (gh run list --workflow release.yml --limit 1 --json databaseId,event,headBranch | ConvertFrom-Json)
gh run view $id.databaseId --json status,conclusion
gh run view $id.databaseId --json jobs      # per-step table
```

`gh run watch` can hang past completion here; poll instead. Budget **25–30 minutes**: gate ~10, each platform build ~5–6, publish ~1. Jobs: resolve → gate → windows + linux → publish.

### 6. Verify what a user actually gets

```powershell
gh release view vX.Y.Z --json assets                   # exactly 3, none a PDB inside a runtime archive
pwsh scripts/get-changelog-section.ps1 -Version X.Y.Z  # must equal the release body byte for byte
gh release download vX.Y.Z --pattern "*windows.zip" --dir $env:TEMP\rel
Expand-Archive ... ; "$env:TEMP\rel\bin\encro.exe" --version   # exit 0, prints X.Y.Z
```

Then the bucket: clone `Arteiimis/scoop-encro`, confirm `bucket/encro.json`'s `version` and the sha256 of the **downloaded** zip match. On this machine:

```powershell
scoop update            # refresh buckets FIRST — without it `scoop update encro` reports the old version
scoop update encro
encro --version         # exit 0, X.Y.Z
```

## Diagnosing a failed run

- Find the failing step in the per-step table, then `gh run view --job <databaseId> --log-failed`. Per-shard test output is not in the job log; it lives in `build/.test-parallel/<suite>/shard-N.log` or `test-output/ut.log` on the Windows runner.
- On Windows, `ut.log` is authoritative: the JUnit file under-reports failing cases.
- `error:` lines from negative tests (invalid crf, unknown key, pack banana) are expected output, not failures. A run's verdict is the conclusion, never a grep for the word "failed".
- A failed gate (`Tag contract`, `Check the build will stamp…`, `Refuse a version…`) creates nothing, so the tag can be deleted and re-pushed onto the fix. That is not re-pointing a published version.
- Anything that touches the workflow can only be exercised by a real run; use `gh workflow run release.yml --ref main -f mode=dry-run`, which builds and packs both platforms and publishes nothing.

## Repairing a published version

Once a release exists, its tag and its assets are fixed; the repair is a new patch version. Mark the changelog heading `[YANKED]`, flag the entry so it cannot be mistaken for the recommended download, and cut the next patch:

```powershell
gh release edit vX.Y.Z --prerelease --notes-file <regenerated section>
```

Record-only entries (tag at or below 2.0.0) carry no assets by design — they are history, not downloads.

Two manual entry points cover what a tag push cannot, and a third previews them without network writes:

- `gh workflow run release.yml --ref main -f mode=publish -f tag=vX.Y.Z` — publish an existing tag. Create-only: it reports identity instead of overwriting when the release is already there.
- `gh workflow run release.yml --ref main -f mode=backfill -f until=X.Y.Z` — record every tag at or below the boundary, body only.
- `pwsh scripts/record-release-history.ps1 -Until X.Y.Z -WhatIfOnly` — print what the backfill would do.

## Pitfalls

- **Version comparison**: use `[semver]` (`$v = [semver]"2.0.0-alpha.1"`). `sort -V` ranks `2.0.0` *before* `2.0.0-alpha.1`, silently mis-handling pre-releases.
- **Tag counting**: `git ls-remote --tags` prints a `^{}` dereference line per annotated tag — de-duplicate before comparing counts.
- **`git diff --quiet` succeeds on an untracked file**, so an uncommitted first-time manifest looks unchanged. Use `git add` then `git diff --cached --quiet`.
- **The workflow runs the file at the tagged commit.** A workflow fix needs its own commit before the tag exists, else the tag builds the old definition.
- **The bucket manifest describes the published bytes**, not a rebuild — a rebuild differs and the hash check then rejects a correct download.
- **`XMAKE_GLOBALDIR` names the parent** of the directory xmake appends `.xmake` to; set it to `$USERPROFILE` / `$HOME`.
- **Windows packaging** takes the VC redist from the build machine's newest `VC\Redist\MSVC\<ver>\x64\Microsoft.VC*.CRT` and never from `System32`, which may not match the toolchain. `encro.pdb` is stripped from the runtime archive because xpack copies a target's symbol file unconditionally.
- **`git push --follow-tags`** does nothing when the branch has no new commit; push the tag explicitly.
