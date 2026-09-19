## Why

A successful batch encode prints one `Compare: encro preview ...` hint per
encoded file. On the real-world `mimu 26.08` run (628 encoded videos) that was
628 lines of ~170-character absolute paths — roughly 100 KB of terminal scroll
that buries the summary it belongs to. The hint is a single-file affordance
(the `preview` subcommand compares one original/encoded pair), so per-file
repetition adds no information a batch user can act on.

## What Changes

- The post-encode summary prints the `Compare:` preview hint only when its
  encode results carry exactly one successful video. With two or more
  successes the hint block is omitted entirely (no count line, no truncation
  marker naming the omitted ones).
- The rule is a condition on the summary's own result map, not on what this
  process encoded: a resumed run that recovered a single completed task still
  prints its hint, while a batch above one stays silent however many files this
  process encoded.
- The failing path is otherwise unchanged: a run with one success and any
  number of failures prints the failed-file list and the single hint.
- Failed encodes and the attention block keep rendering for every file.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `video-encode-probing`: the "Encode summary hints at preview" requirement
  gains the single-success condition — the hint prints only when the summary's
  encode results carry exactly one successful video, and stays silent above it.
- `plan-output-formatting`: the "Post-encode summary uses the same formatting
  language" requirement carries the same condition into both the full-success
  and the failure path.

## Impact

- `src/video/video_process.cpp` — the hint loop at the end of
  `printEncodingSummary` gains a guard on the `successCount` the function
  already computes.
- `tests/video/video_process_orchestration_tests.cpp` — the two-success case
  inverts its `Compare:` assertion; the failing-batch case (one success, one
  failure) keeps its assertion and becomes the guard for the single-success
  rule; the single-file case gains a stdout assertion.
- No CLI surface, config, or job-state change. No flag to opt back into
  per-file hints.
