## Context

`printEncodingSummary` (`src/video/video_process.cpp`) ends with a loop over
`vidsRunRes` that prints a `Compare:` hint per successful encode. See
proposal.md — Why for the volume problem. The two constraints that shape the
fix: the summary's result map covers recovered job-state tasks as well as files
encoded in this process, and the summary block is written to stdout as one unit
(`console-output-conventions`), so removing lines must not split it.

## Goals / Non-Goals

**Goals:**

- Suppress the hint block for any run whose summary covers more than one
  successful video, using the count the summary already computes.
- Keep the single-file hint byte-identical to today's output.

**Non-Goals:**

- No flag, config key, or environment variable to re-enable per-file hints.
- No change to the hint's text, stream, or severity kind.
- No new summary content (no "N hints omitted" line).

## Decisions

**D1 — Condition on the summary's success count, not on this process's work.**
The hint loop reuses `successCount` already computed at the top of
`printEncodingSummary`. A resumed run that recovered a single completed task
still prints its hint; a batch of 628 recovered tasks prints none. Alternative
considered: count only files encoded in this process — rejected because the
summary line the user reads reports the whole result map, so the hint should
match the block it lives in rather than the process's slice of it.
**D2 — Skip the block entirely instead of truncating it.**
With more than one success the loop is not entered at all: no first-N hints and
no ellipsis line. A partial list still costs a screenful on large batches and
invites "which files got listed?" reasoning. The `preview` subcommand compares
one original/encoded pair, so a batch user who wants a comparison picks the
pair themselves; the hint's value is single-file convenience.

**D3 — The condition sits in the summary, not in `previewHint`.**
`encodeprobe::previewHint` stays a pure formatter of the two paths; the
single-file rule is a property of the summary block. Keeps the existing unit
test of the formatter valid and puts the branch where the count already exists.

## Risks / Trade-offs

- [A batch user loses the copy-pasteable command] → the encoded files and their
  names are in the summary's output directory, and `encro preview` with one
  input re-probes the source, so the command is recoverable from the docs
  without the hint. Accepted: the 628-line dump is the larger harm.
- [Two of the summary cases assert the hint today] → only the two-success case
  (`tests/video/video_process_orchestration_tests.cpp:348`) inverts its
  assertion; the failing-batch case (`:387`) has one success and one failure, so
  its assertion stays and becomes the guard for the single-success rule.
