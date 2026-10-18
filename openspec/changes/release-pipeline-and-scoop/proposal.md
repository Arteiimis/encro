# Proposal

## Why

encro now has 18 tags and a CHANGELOG covering every one of them, but **not a single Release** — and no one has ever installed the tool from anywhere except a hand-written shim at `D:\scoop\shims\encro.shim` that points into a local build directory. The versioning work is finished; nothing yet turns a tag into something a user can download.

That gap is the whole distance between a repository and a released program. It matters now because the versioning work is finished and nothing consumes it: the tags exist, and every day they stay unpublished the tag line is only a historical record. Publishing also exposes a second gap — the packaging is wrong in a way that would produce a broken download, because `xpack` is configured but ships the executable alone, and the executable cannot start without `onnxruntime.dll` beside it.

## What Changes

- **Fix the package shape.** `xpack` currently declares five formats and copies only the executable. It will ship exactly two artifacts — a Windows `.zip` and a Linux `.tar.gz` — each containing the licence, the README, and on Windows the runtime DLL the binary loads at startup. The extra formats (installer, source archives) go away: GitHub already serves the source, and Scoop consumes zips.
- **Add a release workflow.** A tag push builds both platforms and creates a GitHub Release whose body is that version's CHANGELOG section. It refuses to publish unless the test suite, the tag contract and the version declaration all agree first. Manual entry points exist for the two cases a tag push cannot cover: publishing a tag that already exists, and filling in the tags that have no release entry yet.
- **Complete the release history.** All 18 existing tags get an entry, so the Releases page and the tag list finally agree. Those versions predate distribution, so their entries are their changelog section plus a statement that they were never distributed and carry nothing to download — they are recorded, not announced.
- **Ship the first downloadable version as a new tag.** No existing tag can produce a working package: `v2.0.0` was cut before the packaging was fixed, and a published tag is never moved. The first distributed release is therefore a patch version tagged after this work lands, and `v2.0.0` stands as the recorded milestone it is.
- **Build Windows in CI for the first time.** Windows has never been built by CI; the release workflow adds it, reusing the local `build-repo` package definition for the ONNX runtime and caching the unpacked package between runs.
- **Publish a Scoop manifest.** A new `Arteiimis/scoop-encro` bucket gains a manifest that installs the Windows zip and generates its own shim, replacing the hand-written one. Each release updates the manifest automatically; if that cross-repository step fails it must not take the Release down with it.
- **Document installation.** The README gains a section a stranger can follow: Scoop for Windows, the tarball for Linux, what the two platforms can and cannot do, and the ffmpeg requirement.

## Capabilities

### New Capabilities

- `release-pipeline`: what a published release consists of and what must be true before one is made — the two artifacts and their contents, the naming that ties an asset to its tag, the gates a release must pass, the trigger surface (tag push plus a manual entry point), the Release body derived from the CHANGELOG, and the rule that publishing the same version twice is a no-op rather than an overwrite.
- `scoop-distribution`: how the tool reaches Windows users through a Scoop bucket — the manifest's shape and version source, the mapping from a tag to a manifest version and asset URL, the shim the bucket generates in place of the hand-written one, how a release updates the bucket, and what a failure in that step leaves behind.

### Modified Capabilities

None. `ci-artifacts` describes what the CI pipeline publishes about a test run (reports, coverage); it says nothing about release artifacts, and this change leaves it untouched.

## Impact

- `xmake.lua` — the `xpack` block: formats reduced to two, licence and runtime files added to the package contents.
- `.github/workflows/release.yml` — new. Builds Linux and Windows, gates on the tests, the tag contract and the version agreement, then creates the Release and updates the bucket.
- `scripts/verify-release-tags.ps1` — gains the entry point it currently lacks; it becomes part of the release gate rather than a script nobody runs.
- The GitHub Releases page — one entry per existing tag, none of them downloadable, plus the first distributed release published by the new pipeline.
- `CHANGELOG.md` — a section for the first distributed version.
- `README.md` — an installation section, and the platform difference (Windows can organize, Linux builds the tagger stub and cannot).
- New repository `Arteiimis/scoop-encro` and a repository secret for writing to it.
- The existing `D:\scoop\shims\encro.shim` stops being the source of the installed command; the bucket's generated shim replaces it.
- No source-level behaviour changes: the binary is the one already tested, packaged differently.
