# Tasks

## 1. Windows runner spike

- [ ] 1.1 Add the Windows build job to a new `.github/workflows/release.yml` with a `mode=dry-run` dispatch that builds both platforms but publishes nothing, then run it once and record the result — verified by a completed run whose Windows job builds `encro` with clang-cl and reports its wall-clock time in the job summary, and whose Linux job produces the tarball; the measured time decides whether D10's cache is enough or the job needs splitting.
- [ ] 1.2 Record in this file's group record what the cold run cost and whether the ONNX package cache restored on a second run — verified by two consecutive runs, the second noticeably faster on the Windows job.

## 2. Package shape

- [ ] 2.1 Rewrite the `xpack` block: formats reduced to `zip` and `targz`, `set_basename("encro-$(version)-$(arch)-$(plat)")`, and `add_installfiles` carrying `LICENSE` and `README.md` — verified by `xmake xpack encro` producing `encro-2.0.0-x64-windows.zip` and `encro-2.0.0-x64-linux.tar.gz` (names read from the actual output, not assumed) and by listing each archive's contents.
- [ ] 2.2 Add the startup runtime library to the Windows package without the load-on-demand providers — verified by unpacking the zip and confirming `onnxruntime.dll` is present while `onnxruntime_providers_cuda.dll` and `onnxruntime_providers_shared.dll` are absent.
- [ ] 2.3 Prove the Windows archive is self-sufficient — verified by unpacking it into an empty directory outside the build tree and running `encro --version` there, which must print the version rather than fail on a missing DLL.
- [ ] 2.4 Add the platform-capability and prerequisite section to `README.md` so it travels inside both packages — verified by unpacking each archive and reading the section, which must state the organize limitation on Linux and name ffmpeg/ffprobe as required.

## 3. Release workflow

- [ ] 3.1 Add a changelog-section extractor to the workflow and verify it on every existing version — verified by extracting all 18 sections and confirming each starts with its version's `## [<version>] - <date>` heading and contains no text from a neighbouring version.
- [ ] 3.2 Add the gate job: `xmake test-report --suite=all` plus `pwsh scripts/verify-release-tags.ps1`, with both platform builds depending on it — verified by a dry-run where the build jobs are skipped when the gate is made to fail deliberately, and by a passing run where both proceed.
- [ ] 3.3 Add the publish path for a tag push and for `mode=publish` with an explicit tag, publishing both artifacts and setting the body to the extracted section — verified by the Release existing after a run, carrying exactly two assets and a body equal to the changelog section.
- [ ] 3.4 Add `mode=backfill`, which creates an entry for every tag that has none, with the never-distributed paragraph and no assets — verified by a run that reports the number of entries created and by opening an early version's entry, which has the paragraph, the changelog body and zero assets.
- [ ] 3.5 Make publishing create-only — verified by re-running `mode=publish` for an already-published version and confirming the run reports it as already published while the release body and asset hashes are unchanged.
- [ ] 3.6 Refuse to publish a tag whose version has no changelog section — verified by attempting a publish for a tag with no section and confirming the run fails before creating anything.

## 4. Scoop bucket

- [ ] 4.1 Create `Arteiimis/scoop-encro` with a `bucket/` directory and add its write token as the `SCOOP_BUCKET_TOKEN` secret — verified by `gh repo view Arteiimis/scoop-encro` succeeding and `gh secret list` on the encro repository naming the secret.
- [ ] 4.2 Generate the manifest in the publish path: version without the `v` prefix, the artifact URL, a hash of the built artifact, `bin`, `notes` naming ffmpeg, and `checkver`/`autoupdate` — verified by inspecting a generated manifest and by `scoop checkver` (or an equivalent URL/version comparison) agreeing with the release it was generated from.
- [ ] 4.3 Write the manifest to the bucket after the release, marked continue-on-error — verified by a publish run whose bucket step succeeds and leaves the manifest naming the new version, and by a run with the bucket step forced to fail which still leaves the Release and both assets published.

## 5. First release

- [ ] 5.1 Run `mode=backfill` against the real repository — verified by `gh release list` showing one entry per tag with no entry missing and no tag without an entry, and by every entry before `v2.0.0` having zero assets.
- [ ] 5.2 Run `mode=publish` for `v2.0.0` — verified by the `v2.0.0` release carrying exactly the Windows zip and the Linux tarball, a body equal to its changelog section, and a bucket manifest naming `2.0.0`.
- [ ] 5.3 Install through Scoop on the maintainer machine and confirm the hand-written shim has been replaced — verified by `scoop install` succeeding, `encro --version` reporting the installed release rather than a build-directory binary, and exactly one `encro` command being on the PATH.

## 6. Installation documentation

- [ ] 6.1 Add the installation section to `README.md`: Scoop for Windows, the tarball for Linux, the ffmpeg prerequisite, and how to read the Releases page (which versions are downloadable and which are recorded history) — verified by following the section verbatim on a clean environment and reaching a working `encro`.

## 7. Integration verification

- [ ] 7.1 Verify the release chain end to end from a freshly pushed tag — verified by tagging a patch version, pushing it, and observing an unattended release with two assets, a correct body and an updated bucket manifest, with no manual step in between.
- [ ] 7.2 Verify the guard rails hold on the real repository — verified by a repeat publish of the already-published `v2.0.0` leaving it byte-identical, and by `pwsh scripts/verify-release-tags.ps1` still exiting 0.

## Workflow follow-up

- Run the code review for this change once the implementation is committed and self-verified, then `openspec-verify-change` before it.
- Archive `backfill-version-history` and this change **together** in one `openspec-bulk-archive-change` run — the two are one release story, and archiving A alone would leave the repository claiming a published release it cannot make.
