## 1. Subprocess stderr capture

- [ ] 1.1 Add failing `exec2` tests: with merging disabled, a child writing to stderr populates the result's separate stderr field (full text, nothing forwarded to the caller's stderr), the merged output still holds only stdout, and the empty case stays empty; with merging enabled the separate field stays empty and stderr lands in the merged output (spec: subprocess-exec). Then add the `stderrText` capture to `exec2` in `src/utils` (design D1) and verify with the subprocess unit suite
- [ ] 1.2 Add a failing test for the shared reason-extraction helper (first classifier-accepted diagnostic line — else the last non-empty line — from `stderrText` when set, else from the retained merged-output tail; trimmed, ~200-char cap; `exit code N` fallback when nothing carries a line) and implement it next to `ExecResult` (design D2); verify with the subprocess unit suite

## 2. Failure reasons recorded and surfaced

- [ ] 2.1 Add failing tests: an encode or probe task whose ffmpeg fails records the child's first diagnostic line as the task failure reason (fake tool emits the line to its merged output and exits non-zero), logs it at warning level with the input, and the post-run failed-file list prints `path: reason` per file (spec: error-visibility "Task failure reasons name the cause"). Then wire the reason through `src/video/video_encode_runner.cpp` and `video_batch_execution.cpp` (retaining a bounded merged-output tail on failure, design D2) into the failed-task list, and verify with the video batch suite
- [ ] 2.2 Extend the picture compression failure path the same way (`src/picture/picture_compress.cpp`, `picture_process.cpp`): failed compressions list with reasons; `All picture compressions failed.` keeps its error line and gains the per-file list. Verify with the picture suite

## 3. Crash visibility

- [ ] 3.1 Add a failing test: an unhandled exception prints a one-line crash reason plus the log path to stderr (log-file tier still written; exit non-zero), and a broken log directory still yields the stderr line. Then change `writeCrashMessage` in `src/infra/crash_runtime.cpp` to always run the stderr tier (design D3), and verify with the crash-handling tests; rebase coordination with `hardening-crash-diagnostics` if it lands first

## 4. Regression fixes

- [ ] 4.1 Concat manifest: add a failing `[real-ffmpeg]` e2e test (the fake tool never parses manifests, so only real ffmpeg pins this) encoding with a relative `-o out` asserting the final MP4 exists and is valid — it fails today with the doubled-path error — plus a unit test that manifest entries resolve from the manifest's directory; write bare segment names in `list.txt` (design D4). Verify with `xmake test-report --tag="[real-ffmpeg]"` plus the e2e suite
- [ ] 4.2 Compression temp extension: add a failing `[real-ffmpeg]` e2e test that `-c` compression of a PNG succeeds end to end (fails today on `.jpg.partial`), and a unit test pinning the temp-name pattern (`<stem>.partial.jpg` → rename). Apply the rename-pattern fix in `src/picture/picture_compress.cpp` (design D5) and verify the atomicity tests still pass
- [ ] 4.3 Preview output guard: add a failing test that `preview --output bare-name.mp4` writes to the working directory and exits 0 (crashes today), and keep the nested-directory creation test green; skip `create_directories` on an empty parent path in `src/preview/preview_process.cpp` (design D6). Verify with the preview suite

## 5. End-to-end verification

- [ ] 5.1 Run `xmake build e2e_tests && xmake run e2e_tests` and `xmake test-parallel`; confirm the three regressions stay fixed and no suite regresses (fake-tool stderr coverage included)
