## Context

Current resume is per-file: `TaskRecord` per input video (one ffmpeg pass), `normalizeExistingTask` marks Interrupted/Pending tasks with an existing target as Succeeded via `actionTargetExists`, and `needsExecution` skips only Succeeded tasks. `lastFrameCount` is persisted (throttled to 2 s) but never consumed. ffmpeg writes directly to the final MP4, so an interrupted run leaves a truncated, unplayable file that cannot be salvaged. See proposal.md - Why.

## Goals / Non-Goals

**Goals:**
- Resume an interrupted MP4 video encode from the first uncompleted segment (~10 s granularity) instead of the file start.
- Segment progress survives crash with at most one segment of rework.
- Uniform code path: segmentation applies to every MP4 video encode, resumed or not.

**Non-Goals:**
- Frame-exact byte-level resume (NVENC cannot start mid-GOP; keyframe-aligned segments are the practical ceiling).
- Fixing the pre-existing "partial output counts as success" gap for non-segmented tasks.
- A user-configurable segment duration knob.
- Changing WebP/picture or archive behavior.

## Decisions

### D1: Audio-stripped segmented encoding with TS intermediates + final assembly

Each video encode is: extract audio once, loop fixed-duration video-only segments (one ffmpeg invocation per segment), then one assembly pass that concatenates the video and muxes the audio back:

```
extract: ffmpeg -i "<input>" -vn -c:a copy "<tmpdir>/audio.<ext>"
                 # fallback if copy fails: -c:a aac (encode once)
segment k: ffmpeg -ss <t> -i "<input>" -t <segDur> -force_key_frames 0 \
                   -an -c:v <codec> -crf <crf> ... -f mpegts "<tmpdir>/seg_<k>.ts"
final:     ffmpeg -f concat -safe 0 -i "<tmpdir>/list.txt" -i "<tmpdir>/audio.<ext>" \
                   -map 0:v -map 1:a -c copy -y "<output>"
```

- `-ss <t>` before `-i`: keyframe input seek (fast). With `-an`, audio is not decoded during segmenting.
- `-force_key_frames 0`: forces a keyframe at output time 0 (each segment's output timeline starts at 0 after the input seek), so the segment starts with a keyframe exactly at the segment boundary, each frame is encoded exactly once, and segments are independently decodable. (`pts:<t>` syntax is rejected by ffmpeg for time 0 — "Invalid keyframe time".)
- Segments are video-only (`-an`); the audio track is processed exactly once (losslessly copied when possible) and muxed back in the final `-map 0:v -map 1:a` pass, so it stays on the original timeline and A/V sync is guaranteed (see D8).
- Inputs without an audio stream skip extraction and the final pass omits `-i audio / -map 1:a`.
- **Why TS over fMP4 segments?** The concat demuxer is battle-tested with TS; no moov/moof/`empty_moov` edge cases. The final re-mux (`-c copy`, one fast pass) produces the MP4.
- **Alternatives rejected:**
  - *Single pass + `-ss` resume*: interruption leaves a truncated MP4 with no moov; resuming mid-GOP is impossible for NVENC without re-encoding a keyframe-boundary region, which double-encodes frames and needs stitching anyway. Segmentation encodes every frame exactly once.
  - *fMP4 segments*: works but TS is simpler to reason about for concat.
  - *Audio inside segments* (original design): per-segment AAC re-encoding introduces priming glitches at boundaries and per-segment A/V offsets that can accumulate across many segments. Stripping audio removes both.

### D2: Per-segment ffmpeg invocation (not the segment muxer)

One process per segment, reusing the existing `exec2`/progress-file/monitor machinery per invocation. **Why not `-f segment` with `-segment_start_number`?** The muxer's split points drift from the nominal grid (cuts at the keyframe *after* the target time), so segment boundaries would accumulate error; explicit invocations give exact, measured boundaries via each segment's progress file.

### D3: New optional TaskRecord fields `segmentIndex` and `resumeTimeUs`

- `segmentIndex` (uint64): count of fully-encoded segments (next segment to encode).
- `resumeTimeUs` (uint64): cumulative encoded duration of completed segments (from each segment's `-progress` `out_time_us` at `progress=end`); next segment starts at `resumeTimeUs`.
- **Why not reuse `lastFrameCount`?** It is flushed at most every 2 s (throttle), can lag a segment boundary, and frame counts don't map to segment indices when segments vary in length (keyframe-aligned cuts).
- Old state files lack these fields → `nullopt` → file-level resume behavior (spec: "Old state without segment records"). No schema version bump needed; fields are optional.

### D4: Forced flush at segment boundaries

The existing `markProgress` throttle (2 s) stays for intra-segment updates; segment completion calls a forced flush so a hard crash loses at most the in-flight segment. Per-segment start time comes from `resumeTimeUs`, not from the stale `lastFrameCount`.

### D5: Per-task temp segment directory

`%TEMP%/encro/segments_<task-id-hash>/` containing `seg_<k>.ts`, `list.txt`, and the extracted audio file. Survives interruption (never cleaned on failure); deleted after successful assembly; deleted on `--restart` alongside `clearExecutionState`. Missing files (segments or audio) are detected on resume: missing audio → re-extract; missing segments → re-encode from the first gap (spec: "Missing temp segment files" / "Audio temp file missing on resume").

### D6: Segment-based restore in `normalizeExistingTask`

For tasks with segment records, success restore keys off segments + final output, not `actionTargetExists` alone: segments complete + final MP4 missing → concat-only rerun; segments incomplete → continue from `segmentIndex`. This closes the partial-MP4 false-success hole for segmented tasks.

### D7: Segment-aware progress

`EncodingState` gains a base-frame offset (from `resumeTimeUs` × fps at segment start); the monitor's `markProgress` reports `baseFrames + currentSegmentFrames`. Total frames still come from the existing lazy ffprobe probe.

### D8: Audio handled once, outside segments

The audio track is extracted once at task start (`-vn -c:a copy`, fallback to a single `-c:a aac` encode if the copy is not container-compatible) and muxed back in the final assembly. Segments carry `-an`. **Why?** Per-segment audio (original design) re-encodes AAC N times — each boundary gains priming artifacts — and each segment's audio stream has its own relative timeline, so the concat demuxer's per-file duration offsets can drift A/V sync over hundreds of segments. A single pristine audio track on the original timeline muxed against the concatenated video avoids both. Copying (rather than re-encoding) is also lossless and faster. **Alternatives:** keep audio in segments (rejected, above); per-segment `-c:a copy` (not frame/sample-safe with `-ss` on the input).

## Risks / Trade-offs

- [Rate-control reset per segment (NVENC CRF)] → CRF is per-frame; boundary variance is negligible. Accepted.
- [New failure point: assembly step] → `-c copy` is fast and idempotent; resume re-runs it; task only becomes Succeeded after assembly succeeds.
- [Audio copy incompatibility (e.g., unusual source audio)] → Single fallback encode (`-c:a aac`) before segmenting; audio is still encoded at most once.
- [Audio extraction adds one I/O pass over the input] → Fast copy pass; interrupted extraction is idempotent (re-extract on resume).
- [TS intermediates: ~2× output transient disk usage] → Segment dir deleted after assembly.
- [Container metadata differences (TS → MP4 re-mux)] → Cosmetic; acceptable.
- [`-ss` input seek may land at keyframe < segment start] → Dropped frames are decode-only; `-force_key_frames` guarantees the output starts exactly at the segment boundary.

## Migration Plan

No breaking changes: existing state files resume with file-level granularity; all new MP4 video encodes use segmentation. Rollback is a revert — segmentation is entirely internal to the encode pipeline.

## Open Questions

None.
