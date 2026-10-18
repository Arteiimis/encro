# Design

## Context

`backfill-version-history` left the repository with 18 semantically-versioned tags, a CHANGELOG whose every tag carries its own section, and a binary whose `--version` is generated from one declaration. None of that reaches a user: there is no release, no downloadable artifact and no package manager entry.

Three facts about the current state shape every decision below.

`xpack("encro")` already exists and is wrong in two ways. It declares five formats (`nsis`, `srczip`, `srctarxz`, `zip`, `tarxz`) where two are wanted, and it packages only `add_targets("encro")` — the executable — while `encro.exe` has a **load-time** import on `onnxruntime.dll`. A package built from the current block starts on the maintainer's machine (where `copyOrtRuntimeDlls` has already placed the DLL next to the binary) and fails on a clean one. Verified against `plugins/pack/xpack.lua` in xmake 3.1.2: `licensefile` is only a specvar consumed by the inno and wix generators, so a zip does not get the licence unless `add_installfiles` puts it there.

Windows has never been built by CI. `.github/workflows/ci.yml` runs `ubuntu-24.04` only. The ONNX runtime comes from `build-repo`, a package definition committed to this repository (`xmake.lua:90`), which downloads a 314 MB zip and unpacks a 1.9 GB tree — so the Windows job is a caching problem, not a feasibility problem.

The historical tags cannot produce artifacts. Each tag carries its own `xmake.lua`: `v0.1.0` has none of the `xpack` machinery, and nothing before `v2.0.0` has the ONNX dependency at all. Rebuilding 17 historical versions would mean 17 archaeological builds against long-gone dependency versions, with no way to fix a failure that is not a rewrite of history.

## Goals / Non-Goals

**Goals**

- One command obtains a working encro on Windows; one archive obtains it on Linux.
- The Releases page and the tag list describe the same history.
- A release cannot be created by accident, and cannot be created twice with different contents.
- The first distributed release is obtainable without any local build.

**Non-Goals**

- Building artifacts for historical versions. Only `v2.0.0` onward ships binaries.
- A CUDA-enabled package. The package runs organize on the CPU execution provider; the larger GPU stack stays a documented opt-in.
- Publishing to any package manager other than Scoop.
- Automating the ffmpeg prerequisite.

## Decisions

### D1 — A release entry exists for every tag; only distributed versions carry artifacts

The Releases page will list all 18 tags. A version is *distributed* if it is at or after the first version this pipeline publishes (`v2.0.0`); every earlier version gets an entry whose body is its changelog section prefixed by one paragraph stating that it was never distributed and that nothing is downloadable. The distinction is carried by the presence of assets, not by a separate GitHub "prerelease" flag — those versions were not prereleases, they were simply never published.

This keeps `v2.0.0`'s "first public release" wording true in the only sense that matters: it is the first version anyone can obtain.

### D2 — Two formats, named from the tag

`set_formats("zip", "targz")`, with `set_basename("encro-$(version)-$(arch)-$(plat)")`. xmake composes the archive name as `basename() .. extension()`, and `basename()` already defaults to `<name>-<version>`; the DSL allows overriding it and filtering `$(version)`, `$(arch)` and `$(plat)` through the builtin variable handler (verified in `plugins/pack/xpack.lua`). That yields `encro-2.0.0-x64-windows.zip` and `encro-2.0.0-x64-linux.tar.gz`.

`x64` rather than `x86_64`: it is what `os.subarch()` returns on both runners, so no mapping layer is introduced between what xmake reports and what the filename claims. The `v` prefix is dropped because Scoop's version field must be bare semver, and having the asset and the manifest agree removes a mapping step that could drift.

### D3 — The package contains the startup dependency and nothing that loads on demand

Windows: `encro.exe`, `onnxruntime.dll`, `LICENSE`, `README.md`. Linux: `encro`, `LICENSE`, `README.md`.

`copyOrtRuntimeDlls` currently copies three DLLs into the build directory — the startup library and two provider libraries that are only loaded when a CUDA execution provider is registered. Packaging all three would add ~307 MB to satisfy a capability the light package does not offer. The package therefore selects `onnxruntime.dll` explicitly rather than packaging whatever sits beside the executable, and the build-directory copy stays as it is so local CUDA work is unaffected.

The licence and README go in through `add_installfiles`: `licensefile` is a specvar for installer generators, not a file that lands in a zip.

### D4 — Gates run before anything is built

`xmake test-report --suite=all` (unit + e2e) and `scripts/verify-release-tags.ps1` run as a job that the build job depends on. Nothing is packaged, uploaded or published when either fails. Ordering the gate first means a broken commit costs one failed job rather than two platform builds followed by a failure.

`verify-release-tags.ps1` gets the entry point it has been missing since `backfill-version-history`: the release gate is exactly the moment when "every tag still satisfies its contract" is worth asserting, and it is the only automated evidence for those scenarios.

### D5 — Three entry points, all funneling into one publish routine

- `push: tags: ['v*']` — the normal path.
- `workflow_dispatch` with `mode=publish` and a tag input — for a tag pushed before this workflow existed.
- `workflow_dispatch` with `mode=backfill` — records every tag that has no release entry, creating no artifacts.

Backfill is a mode of the same workflow rather than a one-off script because it must be re-runnable: creating 17 entries by hand invites a partially completed history that nothing ever notices. Both dispatch modes refuse to run on a tag that does not match the version grammar.

A tag pushed before this workflow existed cannot trigger it — GitHub executes the workflow file as it exists at the tag's commit, and those commits predate `release.yml`. That is why the manual path is needed rather than optional.

### D6 — The release body is cut from the changelog, not regenerated

The body is the text between the version's `## [<version>] - <date>` heading and the next `## [` heading in `CHANGELOG.md`. One extraction used by both the publish path and the backfill path, so a distributed release and a historical entry describe themselves identically apart from D1's prefix paragraph. If the section is absent, the run stops: a release with an invented body would break the changelog contract that `backfill-version-history` established.

### D7 — Publishing is create-only

Before publishing, the run asks whether a release for that version exists. If it does, the run reports it and stops without touching assets or body. GitHub would let a re-run overwrite the body and replace assets; both are exactly what the spec forbids, because a published version's contents must not change. The check is on the release's existence, not on whether the artifacts happen to match, so a re-run can never "fix" a published version either.

### D8 — The bucket update is downstream of the release and cannot fail it

The manifest is written to `Arteiimis/scoop-encro` with a `contents` API call using a fine-grained token scoped to that repository. The step runs **after** the release and its assets are published, and is marked continue-on-error: when the token has expired or the bucket rejects the write, the release stands and the run says the bucket is stale. The alternative ordering — updating the bucket first — would publish a manifest pointing at an asset that does not exist yet, and a dropped connection between the two steps would leave every Scoop user broken.

### D9 — The manifest computes what it can and states what it cannot

`version`, `description`, `homepage`, `license`, `architecture.64bit.url`, `architecture.64bit.hash`, `bin`, `notes`, plus `checkver` and `autoupdate`. The hash is computed from the artifact that was just built and uploaded, so it is always the hash of the bytes a user will download.

`bin` is a list of executable names — this is what replaces the hand-written shim: Scoop generates `encro.shim` in its own shims directory, which is the same directory the current hand-written file occupies, so installing overwrites it rather than competing with it.

ffmpeg is stated in `notes`, not in `depends`. A `depends` entry would make Scoop install ffmpeg into the same root and hand the user a second copy with its own update path; the tool already resolves `ffmpeg` from `PATH` or `--ffmpeg-path`, and the README explains it.

### D10 — The Windows job caches the unpacked ONNX package, not the download

`actions/cache` on `~/.xmake/packages`, keyed by `hashFiles('xmake.lua')` with a prefix restore-key — the pattern `ci.yml` already uses for the Linux legs. The first run pays the full 314 MB download and 1.9 GB unpack; later runs restore the unpacked tree. Keying on `xmake.lua` is deliberate: the ORT version is pinned there, so any change that could invalidate the cached package also invalidates the key.

## Risks / Trade-offs

**The first Windows run is unmeasured.** No one has built this project with clang-cl on `windows-latest`. The cost could be 15 minutes or it could exceed the job timeout, and the cache design above assumes the build itself succeeds. This is a spike task placed first, before any packaging work: if the cold build cannot complete, the whole Windows half of this change is re-scoped rather than discovered to be broken at release time.

**The bucket token is an expiring external dependency.** D8's continue-on-error keeps a stale token from blocking releases, but a stale token means Scoop users silently stop receiving updates until someone reads the run log. Mitigation is that the failure is reported, not swallowed — and that the release remains manually recoverable.

**Historical entries are public and semi-permanent.** Once 17 entries exist they become part of the public history, and deleting them later would itself be a visible event. They are made honest by D1's prefix paragraph, but there is a real risk that someone reads "Release v1.0.0" without the paragraph. Accepted: the alternative — a Releases page showing one entry against 18 tags — is more misleading, not less.

**A published version can never be corrected.** D7 makes publishing create-only, which is what SemVer requires, but it also means a mistake in a shipped package (a missing file, a wrong manifest hash) cannot be fixed in place — it requires a new version. That is the intended discipline; it is called out because the temptation to "just re-run it" will be strong.

**The package's organize capability differs by platform.** Windows ships the ONNX runtime, Linux does not. Users may read a release entry and assume feature parity. Mitigated by putting the limitation in the packaged README (D3), not only in the repository's.

## Migration Plan

1. Create `Arteiimis/scoop-encro` as a public repository with a `bucket/` directory; add a fine-grained token with `contents: write` on it as the `SCOOP_BUCKET_TOKEN` secret. Both are manual and have no automation path.
2. Run the backfill mode: 18 entries appear, none with assets.
3. Run publish for `v2.0.0`: two assets attach, the bucket manifest is written, the entry becomes the first distributed release.
4. On the maintainer machine, install through Scoop, then confirm `encro --version` reports the installed release rather than the local build the old shim points at — and that only one `encro` command answers.
5. Thereafter, pushing a tag is the whole release procedure.

## Open Questions

- The wall-clock cost of a cold `windows-latest` build, and whether the ORT cache key is right, is answered by the spike rather than assumed here.
- Whether a CUDA package variant is worth publishing is out of scope; the light package is defined so a future variant can be added without changing the naming scheme.
