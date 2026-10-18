# Tasks

## 1. Windows runner spike

- [ ] 1.1 Add `.github/workflows/release.yml` with a `mode=dry-run` dispatch that builds both platforms and publishes nothing, then run it once — verified by a completed run whose Windows job builds `encro` with clang-cl and reports its wall-clock time, and whose Linux job builds `encro`; the measured time decides whether D10's single cache is enough or the job needs splitting.
- [ ] 1.2 Run it a second time and compare — verified by the Windows job being materially faster on the second run, proving the ONNX package cache restores, with both durations recorded in this file's group record.
- [ ] 1.3 Confirm the Windows package's VC-runtime source and version before anything else depends on it — verified by locating the redistributable directory inside the installed Visual Studio (`vswhere` on a runner, the same layout locally), confirming all five imported DLLs are present there with byte sizes recorded, and confirming the directory's toolset version is at least the one the build links against; the shipped copies must come from that directory, never from `C:\Windows\System32`, so that the version matches the build instead of the machine.

## 2. Package shape

- [ ] 2.1 Rewrite the `xpack` block: `set_formats("zip", "targz")`, `set_basename("encro-$(version)-$(arch)-$(plat)")`, and `add_installfiles` for `LICENSE` and `README.md` — verified on both platforms: `xmake pack -f zip` on Windows yields one archive named `encro-<declared version>-x64-windows.zip` and `xmake pack -f targz` on Linux yields `encro-<declared version>-x86_64-linux.tar.gz`, with each filename read from the actual output and both architecture tokens recorded in the group record.
- [ ] 2.2 Make the runtime archives match the intended list exactly: `bin/encro.exe`, `bin/onnxruntime.dll`, the five VC-redistributable DLLs, `LICENSE`, `README.md` on Windows and `bin/encro`, `LICENSE`, `README.md` on Linux — verified by listing both archives, which must contain no `encro.pdb`, no `onnxruntime_providers_*.dll`, no `ucrtbase.dll` or `api-ms-win-*` forwarder (those are operating-system components), and nothing else; the ONNX library needs no explicit install rule because xpack already carries the target's linked dependencies, so this task removes the symbol residue, adds the runtime DLLs from task 1.3's source, and adds the documentation files.
- [ ] 2.3 Package the debug symbols as their own asset rather than discarding them — verified by a symbol archive existing for the Windows release whose contents include `encro.pdb`, and by unpacking it beside the extracted runtime archive and confirming the executable resolves a synthetic crash trace to `module!function` form instead of `module+offset`.
- [ ] 2.4 Prove the Windows runtime archive is self-sufficient — verified by unpacking it into an empty directory outside the build tree, on a machine or container where the Visual C++ redistributable is **not** installed, and running `bin\encro.exe --version` there: it must print the version rather than fail on a missing library. A `PATH`-based check with the redistributable merely absent from `PATH` is not sufficient — the loader searches the system directories first — and the archive's compressed size must still land near the executable plus its libraries rather than a multiple of them.
- [ ] 2.5 Add the platform-capability, prerequisite, archive-layout and installation section to `README.md` so it travels inside both packages, and correct its existing CUDA paragraph for the packaged build — verified by unpacking each archive and reading that section, which must state how `organize` behaves on Linux (it starts and leaves every image uncategorized rather than refusing to run — the wording must not promise a clean error), where the executable lives inside the archive, that ffmpeg and ffprobe must already be installed, that the package carries no CUDA provider so organize runs on the CPU provider unless the user adds one, that the Visual C++ runtime DLLs are bundled and redistributed under the Microsoft licence, and which Releases entries are downloadable versus recorded history; and by following the installation steps on a clean environment and reaching a working `encro`.

## 3. Release workflow

- [ ] 3.1 Add a changelog-section extractor and verify it on all 18 released versions — verified by extracting each section and confirming it starts with its own version heading, ends before either the next section heading or the link-definition block, and contains no text belonging to another version — including `0.1.0`, whose section is followed by the link block rather than by another heading; the unreleased section is deliberately not extractable because no tag names it.
- [ ] 3.2 Add the gate job that both builds depend on: `xmake test-report --suite=all`, `pwsh scripts/verify-release-tags.ps1`, and a check that the declared version equals the tag minus its `v` — with a full-history, all-tags checkout, since the script reads `git tag -l` and `git rev-list main --first-parent` — verified by a dry-run where making any one of the three fail stops the run before packaging, and by a passing run where both builds proceed.
- [ ] 3.3 Run the same suite in the Windows job before packaging — verified by a run whose Windows job executes the tests and reports their counts, and whose failure stops that job before `xmake pack` runs.
- [ ] 3.4 Add the publish path for a tag push and for `mode=publish` with an explicit tag: build, package one runtime archive per platform (`-f zip` on Windows, `-f targz` on Linux) plus the Windows symbol archive, and create the release with the extracted section as its body — verified by the release existing with exactly three assets whose names carry that platform's own architecture token (`.zip`, `.tar.gz`, and a symbols archive), by the body equalling the changelog section, by a tag not matching the version grammar being ignored without any release attempt, and by an on-demand publish of a version at or below the recorded boundary being refused with that reason.
- [ ] 3.5 Add `mode=backfill` with an `until` version, which creates an entry for every tag at or below that version that has none, with the never-distributed paragraph and no assets — verified by a run reporting how many entries it created, by an early version's entry carrying the paragraph with zero assets, and by a tag above the boundary remaining entry-less after the run.
- [ ] 3.6 Make publishing refuse to write over an existing release, while still reporting whether a rebuild matches — verified by re-running `mode=publish` for a published version: the run reports it as already published, compares the freshly built hashes with the published ones, and leaves the body and assets unchanged.
- [ ] 3.7 Refuse to publish a tag whose version has no changelog section — verified by attempting a publish for such a tag and confirming the run fails before creating anything.

## 4. Scoop bucket

- [ ] 4.1 Create `Arteiimis/scoop-encro` with a `bucket/` directory and add its write token as the `SCOOP_BUCKET_TOKEN` secret — verified by `gh repo view Arteiimis/scoop-encro` succeeding and the secret listed on the encro repository.
- [ ] 4.2 Generate the manifest in the publish path: version without the `v` prefix, the Windows runtime archive URL, a hash of that archive, `bin` naming the executable's path inside the install, `notes` naming both ffmpeg and ffprobe, and `checkver`/`autoupdate` — verified by inspecting a generated manifest, by an automated comparison of its version and URL against the release it was generated from, and by recomputing the sha256 of the downloaded asset and finding it equal to the manifest's hash, so the checksum the installer enforces is proven to be the hash of the bytes a user will actually download.
- [ ] 4.3 Write the manifest to the bucket after the release, marked continue-on-error — verified by a publish run whose bucket step succeeds and leaves the manifest naming the new version, and by a run with that step forced to fail which still leaves the release and both assets published.

## 5. First release

- [ ] 5.1 Run `mode=backfill` against the real repository with `until=2.0.0`, before any new tag exists — verified by `gh release list` showing exactly one entry per existing tag, every entry carrying zero assets, and an entry count equal to the tag count.
- [ ] 5.2 Cut the first distributed release: bump the single version declaration to the patch version, add its changelog section stating that it is the first downloadable version and naming `v2.0.0` as the milestone it follows, commit, and push the tag — verified by the publish path running unattended and producing a release with two assets, the correct body, and a bucket manifest naming that version.
- [ ] 5.3 Install through Scoop on the maintainer machine and confirm the hand-written shim has been replaced — verified by `scoop install` succeeding, `encro --version` reporting the installed release rather than a build-directory binary, and exactly one `encro` command being on the PATH — with the same three checks repeated after an upgrade to the next published version, which must leave exactly one command reporting the newer version.

## 6. Integration verification

- [ ] 6.1 Verify the chain end to end from a freshly pushed tag — verified by tagging a further patch version, pushing it, and observing an unattended release with two assets, a correct body and an updated bucket manifest, with no manual step in between.
- [ ] 6.2 Verify the guard rails hold on the real repository — verified by a repeat publish of an already-published version leaving it byte-identical, a publish attempt with a mismatched declared version being refused, `mode=publish` for `v2.0.0` being refused because 2.0.0 is at or below the recorded boundary, and `pwsh scripts/verify-release-tags.ps1` still exiting 0.

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
| G5 | Ground truth, P1 | xpack installs the executable under `bin/`, so a DLL without a prefixdir would land beside `bin/` rather than beside the executable | **superseded by measurement** — the round-1 fix added the DLL with `{prefixdir = "bin"}`; packing the tree afterwards showed xpack already carries the target's linked dependencies into `bin/`, so D3 now records that the DLL arrives automatically and that only the debug-symbol handling and the documentation files need explicit rules |
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

The second verification pass also found that the spec, as written, *permitted* publishing `v2.0.0` (an on-demand publish of any existing tag) while the design forbade it, and that two scenarios had no task exercising them. Both were fixed: the on-demand requirement now refuses a version at or below a recorded boundary, a scenario states that refusal, and the tasks verify the non-version tag, the upgrade path and the `v2.0.0` refusal.

**No fourth verifier was dispatched** at that point — the skill's cap is two fix→verify rounds and both were used. Every round-three change was a one-line edit whose correctness is checkable by reading it, but it is recorded here as **unverified by an independent reader** rather than claimed as confirmed.

### Round 4 — after the packaging was measured

Packing the real tree invalidated three assumptions and a fourth review was run on the rewritten artifacts (the user asked for it explicitly, after the cap had already been reached). It found ten more problems, all fixed: the Context and the proposal still described a package that "ships the executable alone"; the executable's four VC-redistributable imports made "exe plus its ONNX library" an incomplete package; task 1.3 offered a package-manager dependency that the requirement forbids; the `v2.0.0` refusal had no recorded boundary; the manifest's `bin` named a bare executable while the archive nests it under `bin/`; the gate job would fail on a tag-push checkout because `main` is not resolvable there; the README's Linux claim ("organize unavailable") does not match what the code does (it starts and leaves every image uncategorized); the manifest checksum had no task proving it equals the bytes a user downloads; the ONNX size figures were credited to a file that states no sizes; and the round-3 record cited task numbers that did not carry those checks.

One of the ten was a **spec conflict, not a wording bug**: excluding `encro.pdb` from the runtime archive contradicts the existing `error-visibility` requirement that Windows release builds resolve crash frames to `module!function`. Resolved by publishing the symbols as their own asset (user decision) rather than by weakening that requirement — `Modified Capabilities` stays empty because the symbols remain obtainable for exactly the build that produced a crash.

A fifth review pass has not been run.

### Not checked by any round

- Whether `encro.exe` needs anything beyond the five identified runtime DLLs at load time was answered by inspecting both import tables; what remains unproven is that the assembled archive starts on a machine that never had the redistributable — task 2.4 settles that.
- Scoop's exact behaviour when a non-Scoop-managed `encro.shim` already occupies the target path. Task 5.3 verifies the outcome (one command, reporting the installed version) rather than assuming the mechanism.
- The ONNX package's download and unpacked sizes are not measured anywhere: the package definition states a URL and a sha256 and nothing else. The figures quoted earlier in this file were estimates and have been removed from the artifacts.

## Workflow follow-up

- Run the code review for this change once the implementation is committed and self-verified, then `openspec-verify-change` before it.
- Archive `backfill-version-history` and this change **together** in one `openspec-bulk-archive-change` run — the two are one release story, and archiving A alone would leave the repository claiming a published release it cannot make.
