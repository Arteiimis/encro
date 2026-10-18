# Design

## Context

`backfill-version-history` left the repository with 18 semantically-versioned tags, a CHANGELOG whose every tag carries its own section, and a binary whose `--version` is generated from one declaration. None of that reaches a user: there is no release, no downloadable artifact and no package manager entry.

Three facts about the current state shape every decision below.

`xpack("encro")` already exists and is wrong in three ways, all measured by packing the current tree (`xmake pack -f zip encro` → `build/xpack/encro/encro-2.0.0.zip`, 15,065,278 bytes) rather than reasoned out. It declares five formats where two are wanted. The archive contains `bin/encro.exe`, `bin/onnxruntime.dll` and a 41.5 MB `bin/encro.pdb`, but neither the licence nor the README. And the executable's own import table names four VC-redistributable DLLs — `MSVCP140`, `MSVCP140_ATOMIC_WAIT`, `VCRUNTIME140`, `VCRUNTIME140_1` — which a machine that has never run a Visual C++ program does not have; `xmake.lua` sets `set_symbols("debug")` but no `set_runtimes`, so the CRT is linked dynamically. Measured against the installed xmake's pack generator (`D:\scoop\apps\xmake\current\plugins\pack\xpack.lua`, not a file in this repository): `licensefile` is only a specvar consumed by the inno and wix generators, and the PDB is copied into `bin/` unconditionally by `plugins\pack\batchcmds.lua`, with no declarative exclude in the xpack DSL.

Windows has never been built by CI. `.github/workflows/ci.yml` runs `ubuntu-24.04` only. The ONNX runtime comes from `build-repo`, a package definition committed to this repository (`xmake.lua:90`), which downloads a large prebuilt runtime and unpacks it into a tree several times that size — so the Windows job is a caching problem, not a feasibility problem. The exact sizes are not recorded here because nothing measured them.

The historical tags cannot produce artifacts, and neither can `v2.0.0`. Each tag carries its own `xmake.lua`: `v0.1.0` has none of the `xpack` machinery, and nothing before `v2.0.0` has the ONNX dependency at all. `v2.0.0` is worse than useless as a source of downloads — its `xmake.lua` still declares the five broken formats, so a build from that tag yields exactly the package this change exists to fix. A released version's assets are built from its own tag, and a published tag is never moved; therefore the first version that can be distributed is **a new tag created after this work lands**, not `v2.0.0`.

## Goals / Non-Goals

**Goals**

- One command obtains a working encro on Windows; one archive obtains it on Linux.
- The Releases page and the tag list describe the same history.
- A release cannot be created by accident, and cannot be created twice with different contents.
- The first distributed release is obtainable without any local build.

**Non-Goals**

- Building artifacts for historical versions. Only the versions this pipeline publishes ship binaries; `v2.0.0` and everything before it stay recorded milestones.
- A CUDA-enabled package. The package runs organize on the CPU execution provider; the larger GPU stack stays a documented opt-in.
- Publishing to any package manager other than Scoop.
- Automating the ffmpeg prerequisite.

## Decisions

### D1 — A release entry exists for every tag; only the versions this pipeline publishes carry artifacts

The Releases page will list all 18 existing tags plus whatever this pipeline publishes. Whether an entry carries artifacts is decided by **how it was created**, not by its version number: entries made by the publish path have assets, entries made by the backfill path do not. Every backfilled entry gets a body consisting of its changelog section prefixed by one paragraph stating that the version was never distributed and that nothing is downloadable. The distinction is carried by the presence of assets, not by a GitHub "prerelease" flag — those versions were not prereleases, they were simply never published.

The first distributed version is a new tag cut after this change lands (a patch release, since nothing about the program's behaviour changes — only its packaging). `v2.0.0` stays a recorded milestone: its tree cannot produce a working package, so publishing binaries from it would ship the defect the proposal exists to remove, and re-pointing it would break the tag-immutability rule established by `backfill-version-history`.

### D2 — Two formats, one per platform, named from the declared version

`set_formats("zip", "targz")` declares both formats, and the workflow selects one per platform with `xmake pack -f zip` on Windows and `-f targz` on Linux — declaring a format makes it packaged for **every** platform that runs xmake pack, so the selection cannot be left to the DSL. Archive names come from `set_basename("encro-$(version)-$(arch)-$(plat)")`, which xmake filters through the same builtin variable handler that feeds the version and platform (verified in the installed xmake's `D:\scoop\apps\xmake\current\plugins\pack\xpack.lua`, function `xpack:basename`).

The architecture token is **whatever `os.subarch()` reports on that platform**, which is `x64` on Windows and `x86_64` on Linux — confirmed by the existing CI build path `build/linux/x86_64/`. The two names therefore differ (`encro-2.0.1-x64-windows.zip`, `encro-2.0.1-x86_64-linux.tar.gz`), which is correct: a mapping table between what the toolchain reports and what the filename claims is a second source of truth that can drift. Scoop consumes only the Windows name, so nothing downstream compares the two.

The version in the name comes from the single declaration in `xmake.lua`, not from the tag, so a gate asserts they agree: the release run compares `encroVersion` with the pushed tag minus its `v` and aborts on any difference. Without that check, tagging without bumping the declaration would produce assets named after the previous version and a manifest pointing at them.

### D3 — The package contains only what a user needs to run the tool

Windows: `bin/encro.exe`, `bin/onnxruntime.dll`, the five VC-redistributable DLLs, `LICENSE`, `README.md`. Linux: `bin/encro`, `LICENSE`, `README.md`.

Everything about that list was checked by packing the current tree rather than reasoned out, and two assumptions turned out wrong.

First, the runtime library is **already packaged automatically**. xpack installs a target under `bin/` and carries the shared libraries the target links through `add_packages`; `onnxruntime.dll` arrives that way and needs no `add_installfiles`. The two provider DLLs that `copyOrtRuntimeDlls` also drops into the build directory do **not** appear, because they are not link-time dependencies — they load only when a CUDA execution provider is registered. The light package therefore falls out of the existing dependency graph instead of a hand-maintained file list, which is strictly better: a hand-written list is a list that goes stale.

Second, the debug symbols are kept out of the runtime archive and published beside it. `bin/encro.pdb` is 41.5 MB and would be most of the download for a file no one runs; xpack copies a target's symbol file into `bin/` unconditionally and the DSL offers no declarative exclude, so removing it from the runtime archive is an install-time hook. It is not discarded: the same file is uploaded as its own asset, because the project's own crash reporting promises that a Windows release build's stack traces resolve to `module!function`, and a user reporting a crash needs exactly the symbols their version was built with. Building from source would produce *different* symbols, so "buy a compiler" is not an answer.

Third, and most consequential, the executable does not run on a clean machine even with its ONNX library present. Its import table names four VC-redistributable DLLs, and `onnxruntime.dll` — a prebuilt binary this project does not compile — names four of its own, so the union is five: `MSVCP140.dll`, `MSVCP140_1.dll`, `MSVCP140_ATOMIC_WAIT.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`.

Two facts narrow the choice. Static linking the CRT (`set_runtimes("MT")`) does **not** fix this: it only affects the executables this project compiles, while the ORT library keeps its dynamic dependency, so the package would still fail. And these are genuinely not operating-system libraries — the Universal C Runtime (`ucrtbase.dll` and the `api-ms-win-crt-*` forwarders) has shipped with Windows since 10, which is why those imports are ignorable, but the `VCRUNTIME140`/`MSVCP140` family is part of the Visual C++ Redistributable and is absent from a machine that has never installed one.

The five DLLs are therefore packaged, taken from the Visual C++ redistributable directory that ships inside the Visual Studio installation used to build them (`<VS install>\VC\Redist\MSVC\<toolset>\x64\Microsoft.VC143.CRT\`), so the version is the one the build actually links against rather than whatever a given machine happens to have. They total 0.78 MB. Microsoft permits redistributing these files to licensed Visual Studio users, which this project is by construction — clang-cl builds against the MSVC toolchain — and the README says so.

This is Microsoft's "local deployment", which they do not recommend for libraries that change often, because a locally deployed copy is not updated by Windows Update. For a runtime that changes rarely the trade is worth taking: the alternative makes the download incomplete, and the alternative to *that* — declaring a package-manager dependency — helps only the reader who installs through the package manager while leaving the tarball user to discover the problem from a loader error.

Whichever wins, the requirement is the same: what is downloaded must start on a machine that has never seen encro. The spike's job is to find out which one satisfies it.

The licence and README go in through `add_installfiles`: `licensefile` is a specvar for installer generators, not a file that lands in a zip, so without it a zip carries neither.

### D4 — Gates run before anything is built

`xmake test-report --suite=all` (unit + e2e), `scripts/verify-release-tags.ps1`, and the version-agreement check of D2 run as a job that both builds depend on. Nothing is packaged, uploaded or published when any of the three fails. Ordering the gate first means a broken commit costs one failed job rather than two platform builds followed by a failure.

The gate runs on Linux; the Windows job re-runs the same suite before packaging. Publishing a Windows binary that was never tested on Windows would be the same class of mistake as publishing one that cannot start — the suite is cheap there relative to the ONNX package install, and it is the only thing that exercises that platform's code paths.

The tag-contract script reads `git tag -l` and `git rev-list main --first-parent`, so the gate job checks out with full history and all tags (`fetch-depth: 0`, `fetch-tags: true`) and explicitly checks out the branch rather than the triggering ref — a tag-push checkout leaves `main` unresolvable, which would fail the script on its first line instead of testing anything.

`verify-release-tags.ps1` gets the entry point it has been missing since `backfill-version-history`: the release gate is exactly the moment when "every tag still satisfies its contract" is worth asserting, and it is the only automated evidence for those scenarios.

### D5 — Three entry points, all funneling into one publish routine

- `push: tags: ['v*']` — the normal path.
- `workflow_dispatch` with `mode=publish` and a tag input — for a tag pushed before this workflow existed.
- `workflow_dispatch` with `mode=backfill` and an `until` version — records every tag at or below that version that has no entry, creating no artifacts. The boundary is what keeps it safe to re-run: tags above it are untouched, so a version whose publish failed cannot be reclassified as "never distributed".

Both dispatch modes refuse to run on a tag that does not match the version grammar. `mode=publish` additionally refuses any version at or below **2.0.0** — the recorded boundary, written as a constant in the workflow next to the grammar pattern, because every tag up to and including that one predates the packaging work and would build a package that cannot start. The refusal is a version comparison, not a judgement about whether some tree happens to build.

A tag pushed before this workflow existed cannot trigger it — GitHub executes the workflow file as it exists at the tag's commit, and those commits predate `release.yml`. That is why the manual path is needed rather than optional.

### D6 — The release body is cut from the changelog, not regenerated

The body is the text between the version's `## [<version>] - <date>` heading and whichever comes first afterwards: the next `## [` heading, or a line matching `^\[[^\]]+\]: ` — the link-definition block. The second terminator matters because the oldest section is followed by the link block rather than by another section, and a body that swallowed those 19 lines would contain every other version's name. One extraction is used by both the publish path and the backfill path, so a distributed release and a historical entry describe themselves identically apart from D1's prefix paragraph. If the section is absent, the run stops: a release with an invented body would break the changelog contract that `backfill-version-history` established.

### D7 — Publishing never replaces, but it does report a difference

Before publishing, the run asks whether a release for that version exists. If it does, the run reports it and stops **without writing anything**. GitHub would let a re-run overwrite the body and replace assets; both are exactly what the spec forbids, because a published version's contents must not change.

A re-run still builds its artifacts and compares their hashes with the published ones, then prints whether they match. Refusing to compare would make "the same source produced the same bytes" unverifiable after the fact; silently replacing on a mismatch would break immutability. Reporting the difference and changing nothing satisfies both.

### D8 — The bucket update is downstream of the release and cannot fail it

The manifest is written to `Arteiimis/scoop-encro` with a `contents` API call using a fine-grained token scoped to that repository. The step runs **after** the release and its assets are published, and is marked continue-on-error: when the token has expired or the bucket rejects the write, the release stands and the run says the bucket is stale. The alternative ordering — updating the bucket first — would publish a manifest pointing at an asset that does not exist yet, and a dropped connection between the two steps would leave every Scoop user broken.

### D9 — The manifest computes what it can and states what it cannot

`version`, `description`, `homepage`, `license`, `architecture.64bit.url`, `architecture.64bit.hash`, `bin`, `notes`, plus `checkver` and `autoupdate`. The hash is computed from the artifact that was just built and uploaded, so it is always the hash of the bytes a user will download.

`bin` names the executable by its path inside the installation — `bin\\encro.exe` — because the runtime archive puts it under `bin/` rather than at the root. This is what replaces the hand-written shim: Scoop generates `encro.shim` in its own shims directory, which is the same directory the current hand-written file occupies, so installing overwrites it rather than competing with it.

ffmpeg and ffprobe are stated in `notes`, not in `depends`. A `depends` entry would make Scoop install ffmpeg into the same root and hand the user a second copy with its own update path; the tool already resolves both from `PATH` or `--ffmpeg-path`, and the README explains them.

### D10 — The Windows job caches the unpacked ONNX package, not the download

`actions/cache` on `~/.xmake/packages`, keyed by `hashFiles('xmake.lua')` with a prefix restore-key — the pattern `ci.yml` already uses for the Linux legs. The first run pays the runtime package's full download and unpack; later runs restore the unpacked tree, which is large enough that paying it on every run is the difference between a usable pipeline and an unusable one. Keying on `xmake.lua` is deliberate: the runtime version is pinned there, so any change that could invalidate the cached package also invalidates the key.

## Risks / Trade-offs

**The first Windows run is unmeasured.** No one has built this project with clang-cl on `windows-latest`. The cost could be 15 minutes or it could exceed the job timeout, and the cache design above assumes the build itself succeeds. This is a spike task placed first, before any packaging work: if the cold build cannot complete, the whole Windows half of this change is re-scoped rather than discovered to be broken at release time.

**The bucket token is an expiring external dependency.** D8's continue-on-error keeps a stale token from blocking releases, but a stale token means Scoop users silently stop receiving updates until someone reads the run log. Mitigation is that the failure is reported, not swallowed — and that the release remains manually recoverable.

**Historical entries are public and semi-permanent.** Once 18 entries exist they become part of the public history, and deleting them later would itself be a visible event. They are made honest by D1's prefix paragraph, but there is a real risk that someone reads "Release v1.0.0" without the paragraph. Accepted: the alternative — a Releases page showing one entry against 18 tags — is more misleading, not less.

**A published version can never be corrected.** D7 makes publishing create-only, which is what SemVer requires, but it also means a mistake in a shipped package (a missing file, a wrong manifest hash) cannot be fixed in place — it requires a new version. That is the intended discipline; it is called out because the temptation to "just re-run it" will be strong.

**The first distributable version is a patch release, not `v2.0.0`.** A reader comparing the changelog with the Releases page will see `v2.0.0` recorded without downloads and its patch successor carrying them. The patch entry has to explain the relationship in one sentence, or the arrangement looks like a mistake rather than a consequence of `v2.0.0` being tagged before the packaging existed.

**Backfill is a migration, not a routine path.** It records every tag up to the `until` boundary that has no entry, which is correct exactly once. Its task text and the spec scenario describe it as completing an existing history, and the publish path is the only thing that creates entries for new tags — so a failed publish leaves a visible gap rather than a wrong statement that create-only publishing then refuses to correct.

**The package's organize capability differs by platform.** Windows ships the ONNX runtime, Linux does not. Users may read a release entry and assume feature parity. Mitigated by putting the limitation in the packaged README (D3), not only in the repository's.

## Migration Plan

1. Create `Arteiimis/scoop-encro` as a public repository with a `bucket/` directory; add a fine-token with `contents: write` on it as the `SCOOP_BUCKET_TOKEN` secret. Both are manual and have no automation path.
2. Run the backfill mode with `until=2.0.0`: 18 entries appear, none with assets. It runs **before** any new tag exists, so it cannot capture a distribution attempt as a historical record.
3. Cut the first distributable version: bump the single version declaration, add its changelog section, commit, and push the tag. The publish path runs automatically from it.
4. On the maintainer machine, install through Scoop, then confirm `encro --version` reports the installed release rather than the local build the old shim points at — and that only one `encro` command answers.
5. Thereafter, pushing a tag is the whole release procedure.

## Open Questions

- The wall-clock cost of a cold `windows-latest` build, and whether the ORT cache key is right, is answered by the spike rather than assumed here.
- Whether the packaged archive really starts on a machine that has never had the Visual C++ redistributable installed. The imports are known and the five DLLs are bundled, but no round ever ran the archive on such a machine — task 2.4 does, and it is the only thing that proves the requirement rather than arguing it.
- Whether a CUDA package variant is worth publishing is out of scope; the light package is defined so a future variant can be added without changing the naming scheme.
