## Context

See `proposal.md` - Why. Current state that shapes the approach:

- `src/video/video_encode_runner.cpp` encodes a task by looping segments in `runSegmentedEncoding` and calling `encodeOneSegment` once per 10 s segment: each segment gets its own ffmpeg process, its own `-ss` accurate seek, its own progress file (`seg_<n>.progress`), and its own `-force_key_frames 0`. Segments are mpegts; `assembleSegments` writes a concat manifest and remuxes with `-c copy`; audio is extracted once by `ensureAudioFile`; job state records `segmentIndex` plus cumulative `resumeTimeUs` per segment.
- A single NVENC session already saturates the engine, so concurrency is not a lever. Measured on this machine (RTX 3070 Laptop, Ryzen 7 5800H, ffmpeg 9.0.1, 1080p HEVC `p6 -rc vbr -cq 28 -b:v 0`): 1080p ceiling ~155-163 fps; 1/2/4/8 concurrent sessions 138.9/150.8/154.8/158.4 fps aggregate, i.e. flat.
- The cost that remains is per-segment idle time: NVENC session setup 0.30 s per process (a bare ffmpeg start without encoding is 0.14 s), plus the accurate seek, which decodes and discards frames from the previous keyframe (cost = distance to previous keyframe / decode speed).
- End-to-end on 1080p24 H.264 with 10.4 s GOPs (`-g 250`), probing skipped, 2-minute fixture, samples of `utilization.encoder` every 0.5 s: current design 24.7 s / 116 fps / 74% mean engine busy; raw 12 x 10 s segment loop 23.6 s / 77%; uninterrupted single pass 18.3 s / 94-100%; the single-pass segment-muxer variant below 18.2 s / 94%.

Locally verified ffmpeg behaviors the design depends on (all measured, none assumed):

| Behavior | Result |
|---|---|
| `-force_key_frames` under `hevc_nvenc` | Ignored unless `-forced-idr 1` is also set: keyframes stayed at the source's own positions (10.4167/20.8333); with `-forced-idr 1` they land exactly at 10.0/20.0/30.0 |
| `-segment_list ... csv -segment_list_flags +live` | Written progressively: completed segments appear as rows while the run continues, the in-flight segment is not listed |
| Segment list times | Carry the encoder reorder delay (10.125 at 24 fps, 10.135 at 23.976 fps) although each file holds exactly 240 frames - the listed times are not frame-accurate |
| Resume with `-ss <k*10s> -segment_start_number <k>` | Frame-exact: a full run reproduced 2880/2880 and 1439/1439 frames at 24 fps and 23.976 fps, and an interrupted+resumed run reproduced the source frame count exactly |
| Resumed run's segment list | Starts empty and uses times relative to the resume point; the earlier rows are not preserved by ffmpeg |
| `-reset_timestamps 1` | Each segment file gets a zero-based timeline; concatenating them with `-c copy` reproduces the exact source duration and frame count |

## Goals / Non-Goals

**Goals:**

- Remove per-segment encoder restarts and input seeks from the MP4 video path, so the engine stays busy for the whole file.
- Preserve observable behavior: 10 s cut cadence, resume granularity and semantics, independently decodable segments, lossless assembly, audio handling, and output settings.

**Non-Goals:**

- The probing phase's CPU-side scoring overlap (a separate wall-clock cost, not part of the engine-busy problem).
- Concurrency defaults per worker, encoder preset/quality policy, NVDEC/hwaccel, or replacing mpegts intermediates.
- Removing segmentation itself: per-segment files are the resume unit and stay.

## Decisions

**D1 - One encoder invocation per task attempt, driven by the `segment` muxer.**
The whole file is read once and the muxer writes the segment series. Alternatives considered: (a) longer segments (10 s -> 60 s) - only divides the per-segment cost by six and coarsens resume; (b) running segments in parallel - measured zero aggregate gain on a single encoder engine; (c) implementing the cut in the app while piping frames to one encoder - re-implements what the muxer already does.

Target command shape (video-only, audio unchanged; the `-progress` file keeps its existing work-directory location, which `prepareEncodeExecution` resolves and the batch executor deletes):

```
ffmpeg -hide_banner -nostats -loglevel error -y -i <input> -an \
  <codec/quality/maxrate flags as today> \
  -forced-idr 1 -force_key_frames "expr:gte(t,n_forced*10)" \
  -f segment -segment_time 10 -segment_format mpegts -reset_timestamps 1 \
  -segment_list <segdir>/segments.csv -segment_list_type csv -segment_list_flags +live \
  <segdir>/seg_%d.ts -progress <existing progress file>
```

Segment files keep today's unpadded `seg_<n>.ts` naming (`segmentFilePath` already produces it), so pre-change runs and the fallback path stay compatible; ordering never depends on names because the manifest is built from the segment list (D6).

**D2 - Boundaries stay at fixed 10 s marks, enforced by forced IDRs.**
`-segment_time 10` alone cuts at the source's own keyframes (measured 10.542 s on a 10.4 s-GOP source), which would coarsen resume granularity and stretch segments on long-GOP sources. `-forced-idr 1 -force_key_frames "expr:gte(t,n_forced*10)"` puts the cut at the first frame at or after each 10 s mark, so boundaries are within one frame interval of the mark and each segment still starts with a keyframe.

**D3 - Resume seeks to arithmetic boundaries, never to listed times.**
Resume uses `-ss <completed * 10 s>` plus `-segment_start_number <completed>`, and the recorded `resumeTimeUs` for a segment is the same value (`completed * 10 s`), so job state stays progress metadata rather than a source of seek times. This is the same arithmetic rule as today and is frame-exact because forced IDR placement and accurate seek share the "first frame with PTS >= t" rule. Using the segment list's own times as the seek point loses frames (measured: 3 frames at 24 fps, because the listed end time is offset by the reorder delay). The in-flight segment file is deleted before resuming; the resumed run writes a fresh list and the app merges the new rows after the previously recorded boundaries.

**D4 - Completed segments are detected from the live segment list, persisted in the existing state shape.**
The app watches the segment list for new rows instead of watching per-segment progress files, and force-flushes the same job-state record (`segmentIndex`, cumulative `resumeTimeUs`) at each new row, keeping the existing schema and the "crash loses at most the in-flight segment" guarantee. Two validity rules apply to what the list says: every listed segment file must exist on disk (a listed file that has been deleted externally rewinds the resume point to the first missing index, preserving the existing missing-segment behavior), and when every listed segment exists and only the final output is missing, the encoder is skipped entirely and assembly runs alone (the existing concat-only resume path). If the list is missing (a run interrupted before this change, or a cleaned directory), fall back to segment-file existence, which still works because file naming is preserved.

**D5 - Progress uses the single continuous counter plus the resume offset.**
One `-progress` stream reports frames and out-time for the whole file; the existing percent math (completed-segment frame offset + current counter) is kept, with the offset coming from the recorded resume boundary instead of per-segment files.

**D6 - The concat manifest is built from the segment list, in list order.**
The muxer decides how many segments exist, so the manifest is generated from the list rather than from an arithmetic segment count. This also removes any dependence on file-name padding for ordering, which is why the existing unpadded `seg_<n>.ts` naming can stay.

**D7 - The probe path keeps its own single-window encode.**
Probe windows are one-shot 10 s encodes (`buildProbeSegmentConfig` -> `buildSegmentEncodeConfig`); they are unaffected and keep using the existing segment config builder. The new single-pass builder is used only by the production encode path.

## Risks / Trade-offs

- [Listed segment times are offset from real boundaries] -> Seek only to arithmetic `k * 10 s` boundaries; add a test that an interrupted-and-resumed run reproduces the source frame count exactly.
- [`-force_key_frames` is a silent no-op without `-forced-idr`] -> Always emit both flags; assert on a long-GOP fixture that cuts land near the 10 s marks with each segment holding the expected frame count.
- [Forced IDRs cost bitrate versus a longer natural GOP] -> Measured negligible (55.58 MB versus 55.70 MB on the same fixture) and equivalent to today, which forces a keyframe at every segment start.
- [Per-segment progress files disappear, and `parseSegmentEndUs` had no replacement] -> The live segment list is the new completion signal. `parseSegmentEndUs` loses its last production caller, so remove it together with its unit tests (`tests/video/video_progress_parser_tests.cpp`), the runner fallback-warning test in `tests/video/video_process_orchestration_tests.cpp`, and the fake tool's `ENCRO_FAKE_FFMPEG_PROGRESS_NO_END_TIME` emulation; keep a parser for the segment list covered by unit tests.
- [e2e fake-tool expectations assume one ffmpeg invocation per segment] -> The fake tool must emit a segment list and numbered segment files, and must be able to fail after N completed segments, because the current tests target a segment file name in the command line (`ENCRO_FAKE_FFMPEG_FAIL_MATCH="seg_1.ts"`) which no longer appears when the command uses the `seg_%d.ts` pattern. Update the affected assertions together with the change.
- [Existing tests hard-code segment file names and manifest behavior] -> `tests/e2e/encro_e2e_tests.cpp` (segment deletion, failure injection, restart) and the `writeConcatManifest` unit test in `tests/video/video_process_orchestration_tests.cpp` all change contract; update them in the same commit as the runner rewrite.
- [ffmpeg version differences in `-forced-idr`, `-segment_list_flags +live`, or `-segment_start_number`] -> Real-ffmpeg tests cover the assembled output; the fake-tool path covers app-side logic without depending on muxer internals.
- [Interrupted runs started before this change have no segment list] -> Keep segment file naming identical and fall back to file-existence detection when the list is absent, so pre-change partial runs still resume.

## Migration Plan

No state migration: file names, job-state fields, and the resume rule (arithmetic 10 s boundaries) stay compatible in both directions, so an interrupted run resumes across the change and reverting the commit remains safe. Old runs without a segment list fall back to file existence. Deploy as one code change with its tests; verify with a real-ffmpeg smoke run that the assembled output matches the source frame count and duration.
