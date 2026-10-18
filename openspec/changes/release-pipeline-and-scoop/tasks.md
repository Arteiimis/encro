# Tasks

## 1. Windows runner spike

- [ ] 1.1 Add `.github/workflows/release.yml` with a `mode=dry-run` dispatch that builds both platforms and publishes nothing, then run it once — verified by a completed run whose Windows job builds `encro` with clang-cl and reports its wall-clock time, and whose Linux job builds `encro`; the measured time decides whether D10's single cache is enough or the job needs splitting.
- [ ] 1.2 Run it a second time and compare — verified by the Windows job being materially faster on the second run, proving the ONNX package cache restores, with both durations recorded in this file's group record.
- [ ] 1.3 Determine whether the produced Windows binary still needs the MSVC runtime libraries at load time — verified by inspecting the built executable's imports and by running the unpacked output in a directory where the redistributable is not on `PATH`, with the finding recorded; if the libraries are required, D3's file list and the README prerequisite section both grow.

## 2. Package shape

- [ ] 2.1 Rewrite the `xpack` block: `set_formats("zip", "targz")`, `set_basename("encro-$(version)-$(arch)-$(plat)")`, and `add_installfiles` for `LICENSE` and `README.md` — verified on both platforms: `xmake pack -f zip` on Windows yields one archive named `encro-<declared version>-x64-windows.zip` and `xmake pack -f targz` on Linux yields `encro-<declared version>-x86_64-linux.tar.gz`, with each filename read from the actual output and both architecture tokens recorded in the group record.
- [ ] 2.2 Add the startup runtime library inside `bin/`, beside the executable — verified by unpacking the Windows archive and confirming `bin/onnxruntime.dll` sits next to `bin/encro.exe` while `onnxruntime_providers_cuda.dll` and `onnxruntime_providers_shared.dll` are absent.
- [ ] 2.3 Prove the Windows archive is self-sufficient — verified by unpacking it into an empty directory outside the build tree and running `bin\encro.exe --version` there, which must print the version rather than fail on a missing DLL.
- [ ] 2.4 Add the platform-capability, prerequisite and archive-layout section to `README.md` so it travels inside both packages — verified by unpacking each archive and reading that section, which must state the organize limitation on Linux, where the executable lives inside the archive, and that ffmpeg/ffprobe must already be installed.

## 3. Release workflow

- [ ] 3.1 Add a changelog-section extractor and verify it on all 18 released versions — verified by extracting each section and confirming it starts with its own version heading, ends before either the next section heading or the link-definition block, and contains no text belonging to another version — including `0.1.0`, whose section is followed by the link block rather than by another heading; the unreleased section is deliberately not extractable because no tag names it.
- [ ] 3.2 Add the gate job that the builds depend on: `xmake test-report --suite=all`, `pwsh scripts/verify-release-tags.ps1`, and a check that the declared version equals the tag minus its `v` — verified by a dry-run where making any one of the three fail stops the run before packaging, and by a passing run where both builds proceed.
- [ ] 3.3 Add the publish path for a tag push and for `mode=publish` with an explicit tag: build, package one format per platform (`-f zip` on Windows, `-f targz` on Linux), and create the release with the extracted section as its body — verified by the release existing with exactly two assets whose names carry that platform's own architecture token, one `.zip` and one `.tar.gz`, and a body equal to the changelog section; and by a tag not matching the version grammar being ignored without any release attempt.
- [ ] 3.4 Add `mode=backfill` with an `until` version, which creates an entry for every tag at or below that version that has none, with the never-distributed paragraph and no assets — verified by a run reporting how many entries it created, by an early version's entry carrying the paragraph with zero assets, and by a tag above the boundary remaining entry-less after the run.
- [ ] 3.5 Make publishing refuse to write over an existing release, while still reporting whether a rebuild matches — verified by re-running `mode=publish` for a published version: the run reports it as already published, compares the freshly built hashes with the published ones, and leaves the body and assets unchanged.
- [ ] 3.6 Refuse to publish a tag whose version has no changelog section — verified by attempting a publish for such a tag and confirming the run fails before creating anything.

## 4. Scoop bucket

- [ ] 4.1 Create `Arteiimis/scoop-encro` with a `bucket/` directory and add its write token as the `SCOOP_BUCKET_TOKEN` secret — verified by `gh repo view Arteiimis/scoop-encro` succeeding and the secret listed on the encro repository.
- [ ] 4.2 Generate the manifest in the publish path: version without the `v` prefix, the Windows asset URL, a hash of the built artifact, `bin`, `notes` naming ffmpeg, and `checkver`/`autoupdate` — verified by inspecting a generated manifest and by an automated comparison of its version and URL against the release it was generated from.
- [ ] 4.3 Write the manifest to the bucket after the release, marked continue-on-error — verified by a publish run whose bucket step succeeds and leaves the manifest naming the new version, and by a run with that step forced to fail which still leaves the release and both assets published.

## 5. First release

- [ ] 5.1 Run `mode=backfill` against the real repository with `until=2.0.0`, before any new tag exists — verified by `gh release list` showing exactly one entry per existing tag, every entry carrying zero assets, and an entry count equal to the tag count.
- [ ] 5.2 Cut the first distributed release: bump the single version declaration to the patch version, add its changelog section stating that it is the first downloadable version and naming `v2.0.0` as the milestone it follows, commit, and push the tag — verified by the publish path running unattended and producing a release with two assets, the correct body, and a bucket manifest naming that version.
- [ ] 5.3 Install through Scoop on the maintainer machine and confirm the hand-written shim has been replaced — verified by `scoop install` succeeding, `encro --version` reporting the installed release rather than a build-directory binary, and exactly one `encro` command being on the PATH — with the same three checks repeated after an upgrade to the next published version, which must leave exactly one command reporting the newer version.

## 6. Installation documentation

- [ ] 6.1 Add the installation section to `README.md`: Scoop for Windows, the tarball for Linux, the ffmpeg prerequisite, and how to read the Releases page — which versions are downloadable and which are recorded history — verified by following the section verbatim on a clean environment and reaching a working `encro`.

## 7. Integration verification

- [ ] 7.1 Verify the chain end to end from a freshly pushed tag — verified by tagging a further patch version, pushing it, and observing an unattended release with two assets, a correct body and an updated bucket manifest, with no manual step in between.
- [ ] 7.2 Verify the guard rails hold on the real repository — verified by a repeat publish of an already-published version leaving it byte-identical, a publish attempt with a mismatched declared version being refused, `mode=publish` for `v2.0.0` being refused with the reason that its tree cannot produce a working package, and `pwsh scripts/verify-release-tags.ps1` still exiting 0.

## Planning-artifact review

Three verifier rounds against a single planning-artifact reviewer (the `code-review` skill's two lenses, `Coherence` then `Ground truth`). The reviewer quoted both sides of every conflict, which is what made triage mechanical.

### Round 1 — 10 findings

| # | Lens | Finding | Verdict |
|---|---|---|---|
| C1 | Coherence, P0 | The migration plan made publishing impossible: backfill created a `v2.0.0` entry before anything was published, while publish refuses to touch an existing release | **resolved** — `v2.0.0` is no longer the publish target; the first distributable version is a new tag, and backfill runs before that tag exists |
| C2 | Coherence, P1 | Create-only publishing could not satisfy the scenario requiring a rebuild's difference to be detected and reported | **resolved** — D7 now rebuilds, compares hashes with the published ones, reports the result and still writes nothing |
| C3 | Coherence, P2 | Backfill would permanently record a version whose publish failed as "never distributed" | **resolved in round 2** — the first attempt only stated the risk; a second pass replaced the blanket rule with an `until` boundary |
| G1 | Ground truth, P0 | `v2.0.0`'s own tree still contains the broken `xpack` block, so publishing binaries from it would ship the defect the change exists to remove | **resolved** — the first distributed release is a patch tag cut after this work lands; `v2.0.0` stays a recorded milestone (user decision) |
| G2 | Ground truth, P1 | The design claimed `os.subarch()` returns `x64` on both runners; the Linux build path is `build/linux/x86_64/` | **resolved** — D2 states each platform keeps its own token, and a scenario forbids normalising them |
| G3 | Ground truth, P1 | D2 was titled "named from the tag" while the value comes from `xmake.lua`, and no gate tied the two | **resolved** — D2 retitled, and the version agreement became the third gate with its own scenario and task |
| G4 | Ground truth, P1 | `set_formats("zip", "targz")` packages **both** formats on every platform, so each job would emit two files | **resolved** — the workflow selects one format per platform with `pack -f`; a scenario forbids a platform contributing both |
| G5 | Ground truth, P1 | xpack installs the executable under `bin/`, so a DLL without a prefixdir would land beside `bin/` rather than beside the executable | **resolved** — D3 installs the DLL with `{prefixdir = "bin"}` and the tasks verify `bin/onnxruntime.dll` beside `bin/encro.exe` |
| G6 | Ground truth, P2 | The verification command `xmake xpack encro` does not exist | **resolved** — every verification uses `xmake pack -f <format>` |
| G7 | Ground truth, P2 | The section extractor stops only at the next `## [`, so the oldest section would swallow 19 link-definition lines | **resolved** — the terminator is now "next `## [` heading **or** `^\[[^\]]+\]: `", verified against the `0.1.0` section |

### Round 2 — 6 problems the first fix pass introduced

| # | Finding | Verdict |
|---|---|---|
| N1 | Non-Goals still said "Only `v2.0.0` onward ships binaries", contradicting the decision that `v2.0.0` ships nothing | **resolved** |
| N2 | The third gate reached the spec and tasks but `proposal.md` and design D4 still described two checks | **resolved in round 3** — the first pass fixed two of the three places, the second fixed the Impact line |
| N3 | Task 3.1 said "all 19 versions" although only 18 released versions exist until the new tag is cut | **resolved** — 18 released versions, with the unreleased section explicitly not extractable |
| N4 | Spec contradicted itself: "every tag has one entry" versus "a failed publish leaves no entry" | **resolved in round 3** — the scenario is now scoped to tags below the backfill boundary |
| N5 | The per-platform architecture claim had no verifying task; a Linux archive without `x86_64` would pass | **resolved** — task 2.1 verifies both filenames on both platforms |
| N6 | Two places still counted 17 historical entries and "one entry against 18 tags" | **resolved** |

### Round 3 — last round inside the 2-round cap

The second verification pass also found that the spec, as written, *permitted* publishing `v2.0.0` (an on-demand publish of any existing tag) while the design forbade it, and that two scenarios had no task exercising them. Both were fixed: the on-demand requirement now refuses a tag whose tree cannot produce a working package, a scenario states that refusal, and tasks 3.3, 5.3 and 7.2 verify the non-version tag, the upgrade path and the `v2.0.0` refusal respectively.

**No fourth verifier was dispatched** — the skill's hard cap is two fix→verify rounds and both were used. Every round-three change is a one-line edit whose correctness is checkable by reading it, but it is recorded here as **unverified by an independent reader** rather than claimed as confirmed. `openspec validate --strict` passes and a stale-claim sweep for the terms each round flagged returns nothing.

### Not checked by any round

- Whether `encro.exe` also needs `vcruntime140.dll` / `msvcp140.dll` at load time — no `set_runtimes` is set and clang-cl's default was never measured. It is now Open Question #2 in the design and task 1.3 measures it.
- Scoop's exact behaviour when a non-Scoop-managed `encro.shim` already occupies the target path. Task 5.3 verifies the outcome (one command, reporting the installed version) rather than assuming the mechanism.
- The ONNX package's 314 MB download / 1.9 GB unpacked figures: taken from the package definition, not measured.

## Workflow follow-up

- Run the code review for this change once the implementation is committed and self-verified, then `openspec-verify-change` before it.
- Archive `backfill-version-history` and this change **together** in one `openspec-bulk-archive-change` run — the two are one release story, and archiving A alone would leave the repository claiming a published release it cannot make.
