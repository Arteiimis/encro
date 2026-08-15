# Tasks: Single-Input Preview Mode

## 1. Filtergraph Segment Mode

- [x] 1.1 Add `encodedWindowsAreSegments` to `FiltergraphSpec`/`buildPreviewFiltergraph`/`buildPreviewCommand`: segment mode references `[1+i:v]` per window with `trim=start=0:end=duration` (segment-local PTS); two-input mode unchanged (`[1:v]` with `trim=start=S_i:end=E_i`); unit tests asserting exact graph strings and command lines for both modes (`[preview]`)

## 2. Single-Input Pipeline

- [x] 2.1 Make `PreviewOptions.encoded` optional and relax cmd post-parse validation to one-or-two positionals (zero still errors); `preview -h` usage shows `<original> [<encoded>]`; cmd tests (`[cmd]`)
- [x] 2.2 Add the single-input branch to `preview_process::run`: probe the source via `encodeprobe::probeSingleFile` into a per-run temp dir (RAII cleanup), encode the N windows with the production settings at the chosen CQ (default CQ 28 + informational note when not probed), score each window with the shared quality helper (segment-local PTS), then render through the segment-mode filtergraph; unit tests with the fake toolchain: single-input run writes the output, fallback note when probing fails, stop-signal abort (`[preview]`)

## 3. E2E and Smoke

- [x] 3.1 Extend e2e: fake-toolchain single-input preview (probe → window encodes → comparison video written), zero-positional error; `[real-ffmpeg]` smoke: single-input preview on a ≥40s testsrc video produces a playable file (`[e2e]`, `[smoke]`)

## 4. Docs and Verification

- [x] 4.1 Update README (`encro preview <original> [<encoded>]` with the single-input workflow) and the post-encode summary hint if it mentions the two-input form
- [ ] 4.2 Run `xmake format -k`, full unit + e2e suites, and the post-change code review
