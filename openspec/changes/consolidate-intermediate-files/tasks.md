## 1. Work-directory infrastructure (design D1/D4)

- [x] 1.1 Add `src/core/work_dirs.h` with `resolveWorkRoot(config, inputPaths) -> eh::Result<fs::path>` implementing the anchor rule from design D1 (--output → webp/encoded_webp → single input → lowest common ancestor → empty/cross-drive error with a `pass --output` message).
- [x] 1.2 Add `scratchDir()` returning `%TEMP%\encro\scratch\` and `.encro\` subpath helpers (`segmentsDir(hash)`, `compressCacheDir(quality)`, `jobStatePath()`).
- [x] 1.3 Add the Windows `Hidden` attribute shim (SetFileAttributesW) applied when creating `.encro\`; no-op elsewhere; unit test creates a temp dir, sets it, and asserts the attribute on Windows only. Wire the shim into every `.encro\` creation site (job-state store init, segmentsDir, compressCacheDir) so the attribute is set on first creation and is idempotent on re-runs.
- [x] 1.4 Unit tests for `resolveWorkRoot`: each anchor branch (explicit output, webp root, single dir, single file, multiple inputs sharing a parent, multiple inputs with only a deeper common ancestor, empty input list error, cross-drive error) with fixtures in `tests/` (reuse TempDir).
- [x] 1.5 Rebase `buildDefaultStateFilePath` (`src/core/job_state.cpp`) on `resolveWorkRoot` so job state follows the work root (update existing job-state path tests).

## 2. Scanner exclusion (spec media-scan)

- [x] 2.1 Add dot-prefix exclusion to `media_scanner` (recursive: `disable_recursion_pending()` for dot-directories; non-recursive: skip dot files); no warnings for skips.
- [x] 2.2 Tests: recursive scan skips `.encro\` and legacy `.compress_tmp*` contents without warnings; dot-prefixed file in a normal dir is not matched (media-scan spec scenarios).

## 3. Video intermediate migration (design D2)

- [x] 3.1 Move probe-root creation (`src/video/encode_probe.cpp` createProbeRoot) to `scratchDir()`; update its RAII guard cleanup test.
- [x] 3.2 Move preview probe-root creation (`src/preview/preview_process.cpp`) to `scratchDir()`; update guard cleanup test.
- [x] 3.3 Move VMAF/SSIM log files (`src/video/video_quality.cpp`) into `scratchDir()`.
- [x] 3.4 Move encode progress file (`src/video/video_encode_runner.cpp` prepareEncodeExecution) into `scratchDir()` **and** delete it after the encode completes (fixes the WebP leak).
- [x] 3.5 Rework `src/video/segment_dir.h` into wrappers over `work_dirs.h`: `segmentsDir(hash)` at `<work-root>\.encro\segments\{hash}\`; caller passes the resolved work root from `encodeVideo` context.
- [x] 3.6 Update segment-related tests (`tests/segment_dir_tests*`) and e2e fake-tool assertions that reference `%TEMP%\encro\segments_*` or temp-root progress files; retarget the e2e `probeArtifactCount` assertion (currently counting `encro_probe_*` under the temp root) to the new scratch dir so it cannot silently pass with a zero count after the move.

## 4. Picture workflow migration (spec picture-compress-resume, design D6)

- [x] 4.1 `buildCompressCacheDir` returns `<work-root>\.encro\compress_q{N}\`; thread the resolved work root through `runPicturePackWorkflow` / `executeCompressPackWorkflow`.
- [x] 4.2 Update `prepareCompressTempDir` stale sweep to the new prefix inside `.encro\` and add one-time removal of legacy `.compress_tmp*` dirs under `<work-root>\packed\` (the historical cache location).
- [x] 4.3 Update picture/pipeline tests asserting `.compress_tmp_q{N}` paths (e.g. `pipeline_picture_tests` cache/layout assertions) and retarget the two e2e `ENCRO_FAKE_FFMPEG_FAIL_MATCH=".compress_tmp"` cases to the new cache path so they keep failing as intended.

## 5. Job-state file migration

- [x] 5.1 Default state file path resolves to `<work-root>\.encro\job-state.json` via `resolveWorkRoot`; staging stays `job-state.json.tmp` next to it (unchanged mechanism).
- [x] 5.2 Update job-state path tests and any e2e assertions on the default `encro.job-state.json` location; verify explicit `--state-file` is unaffected.

## 6. Startup sweep and docs

- [x] 6.1 Add startup sweep for `scratchDir()`: delete entries untouched for > 24h (design D3); unit test: fresh file preserved, stale file removed; no effect on `.encro\segments\`.
- [x] 6.2 Update user-facing help/docs (and CHANGELOG) noting the hidden `.encro\` work root, the new default state-file path (BREAKING), and the cross-drive `--output` requirement.
- [x] 6.3 Full verification: `xmake build tests && xmake test-report`, then `xmake build encro && xmake build e2e_tests && xmake run e2e_tests` with `--tag` filters for the touched areas.