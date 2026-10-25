# Design

## Context

See proposal.md — Why.

- `batch::runTasks` (`src/batch/batch.cpp:139`) enumerates the root, then walks the subfolders in name order running `pipeline::run` for each. Everything printed above a confirmation today comes from that subfolder's own pipeline run: `Found <n> picture(s) under <dir>.` (`src/picture/picture_process.cpp:453`, `:765`), `Found <n> video(s) under <dir>.` (`:871`), the probe plan table (`src/video/video_batch_execution.cpp:400`), then the prompt.
- The prompt already belongs to the workflow, not the driver: `confirmPicturePack` (`src/picture/picture_process.cpp:232`), `confirmEncodingStart` (`src/video/video_batch_execution.cpp:517`), and the resumed-encode packing question (`src/video/video_process.cpp:391`) all go through `readUserIptOnce`, which returns the batch's stored answer without printing anything when `RuntimeContext::promptConsent` already holds one (`src/utils/utils.cpp:484`).
- The only per-subfolder pre-check is `hasProcessableMedia` (`src/batch/batch.cpp:105-118`): a bool built from `media::scanByExtensions` matches (`hasMediaFile`, `hasKeptVideo`) or from `hasAnyRegularFile`, whose recursive iterator stops at the first regular file (`:40-58`). It is called once, inside the loop (`:169`), and only by the batch.
- `countProcessableMedia`'s inputs already enumerate the full match lists, so counting costs nothing extra; pack-only is the one mode whose pre-check is deliberately partial.
- `--pack-only` asks no confirmation at all: `runPackOnly` (`src/app/pipeline.cpp:100-128`) calls `pack::execute` and never touches the prompt primitives. `--dry-run` asks none either (`pipeline.cpp:163-176` exits early; the video probe stage returns `DryRun` before the prompt). `--yes` is short-circuited inside `readUserIptOnce`'s underlying `readUserIpt`.

## Goals / Non-Goals

**Goals:**

- Before the answer, the run's real scope is on screen: how many subfolders will be processed, how many entries that is in total, and which subfolder carries which share.
- One scan per subfolder serves both the block and the existing skip decision, so the change adds no filesystem work.
- Scripted and prompt-free runs keep their byte-identical output.

**Non-Goals:**

- Any change to the pack path: pack-only's `-r/--no-recursive` being a no-op and `-t picture -z` failing per subfolder are recorded in proposal.md's Impact as out of scope.
- Moving the confirmation earlier, before a subfolder's scan, or prompting the driver itself: the workflows own the prompt, and a video run's probe plan legitimately precedes the question.
- Changing the question text, adding a batch-specific question, or re-scoping the stored answer.
- Suppressing the phases' own `Found <n> ...` scan lines in a batch.

## Decisions

**D1 — One up-front scan replaces the in-loop pre-check.**

`hasProcessableMedia` becomes `countProcessableMedia(config, dir) -> std::size_t`, where `0` means "nothing processable". It keeps the existing predicate for each combination — picture run: pictures, plus convertible videos when `--video-webp`; video run: videos the encode scan keeps; pack-only: any regular file. `runTasks` runs it over the enumerated subfolders before the loop and stores `{subfolder, entries}` pairs, which the block consumes and the loop reads for its skip decision (`entries == 0`).

Pack-only reports presence as an entry count of `1` and never a real one, documented on the function: its block never prints (D2), and counting a large tree would replace `hasAnyRegularFile`'s early exit with a full walk that `buildDirectoryPackPlan` (`src/pack/packer.cpp:720`) repeats moments later. Any caller that ever wants a real pack-only count has to ask for it explicitly.

- Alternative: keep the bool and add a second counting scan — three scans per subfolder (pre-check, count, pipeline) instead of two.
- Alternative: let each pipeline run report its own count to the driver — the total cannot be printed before the first subfolder then, which is the whole point.
- Alternative: `std::optional<std::size_t>` (`nullopt` = not processable) — reads better at the call site but smuggles the same pack-only sentinel inside a richer type.

**D2 — The block prints only when a confirmation will be asked.**

Condition: at least one subfolder is processable **and** `!packOnly && !yesToAll && !dryRun`; the quiet gate then applies on its own, because the block prints through `Info` (D7). The block's whole purpose is to disambiguate the prompt's scope, so the runs that never prompt skip it: pack-only asks nothing and would pay a full-tree count, `--yes` is the scripted path whose stdout this change leaves alone, and dry-run asks nothing either.

- Alternative: print always — changes every scripted batch's output and adds the pack-only walk for information nothing consumes.
- Alternative: print when the batch has ≥2 processable subfolders — silently withholds the scope exactly when a single subfolder holds thousands of pictures.

**D3 — The block is three stdout lines: header, one row per processable subfolder, and a closing statement.**

```
Batch: 20 picture(s) in 2 of 3 subfolder(s)
  12  D:\root\a
   8  D:\root\b
The confirmation below covers every subfolder in this batch.
```

The header prints as `Info`, rows as `Plain`, the closing line as `Info`, with `terminal::count` and `terminal::path` carrying the values' own styles (no badge, no severity prefix, `console-output-conventions`). Counts sit left-aligned after a two-space indent, the shape `printSummary`'s rows already use (`src/batch/batch.cpp:238-244`).

The header keeps its leading `Batch:` label rather than reading as a bare count line: `console-output-conventions`' "a label ending in a colon followed by its content" rule is scoped to a diagnostic group's severity-marked announcement, not to narration, and the label is what keeps the block from being read as a phase line (`pipeline-narration`).

Per-subfolder rows rather than a bare total: the reported defect is an invisible scope, and a total alone still hides that one subfolder holds 5000 pictures while another holds 2. The rows also report the same quantities each phase's `Found <n> ...` line reports (summed for the picture run that converts videos), so the two never disagree. Media-less subfolders get no row — their existing `Skipping <path>: no processable media.` notice names them at their turn, and the header's `2 of 3` already accounts for them.

The closing line exists because the prompt can sit several lines lower (a video run prints probe bars and the plan table in between) and the prompt's own text must stay byte-identical to the single-directory question.

- Alternative: a rule-bounded table like the probe plan — more chrome than three short lines, and the batch has no column semantics to justify it.
- Alternative: fold the statement into the header — one long line, and the promise loses its emphasis exactly where it matters.

**D4 — The count's noun follows the run's work.**

`video(s)` in a video run, `picture(s)` in a picture run without `--video-webp`, `item(s)` in a picture run with it (pictures and convertible videos then share one number, which is also how the eligibility pre-check treats that combination).

- Alternative: always `item(s)` — vague in the common case and inconsistent with every phase line's noun.
- Alternative: the mode's noun even for the mixed run — labels converted videos as pictures.
- Alternative: two numbers per row (`3 picture(s) + 2 video(s)`) — per-row branching for a rare flag.

**D5 — The counts are scan-time values, and the block claims nothing more.**

Entries are what the run's own scans find. A video run has two ways to end up processing fewer than the block names, and neither can be reflected before the run: probing skips a file whose estimated output exceeds its source (`src/video/video_batch_execution.cpp:376-386`), and the pipeline's scan drops files already encoded as HEVC (`finalizeScannedVids`, `src/video/video_info.cpp:616-627`, which needs the toolchain the pre-check deliberately does not touch). So no line of the block says how many entries will be encoded, converted or kept. The spec pins this as its own requirement, and it is why the closing line promises scope, not work.

**D6 — The pre-pass, the block and the loop all live in `runTasks`.**

`runTasks` owns enumeration today and is the only place that knows the whole batch, so the pre-pass vectors, the print call and the loop read the same local data; `printSummary` stays where it is (`runBatch`, after the run). `batch.h` swaps the pre-check's declaration; `runBatch` and `batchExitCode` are untouched. No new module: one caller, no state to own.

**D7 — `--quiet` hides the block, and the spec says so.**

The block prints through `Info`, which the quiet gate suppresses (`src/infra/terminal.h:145-161`), while the prompt prints through `Plain` and still appears. A quiet batch therefore asks its single question with no scope above it, which is what `--quiet` means (narration off, prompts intact) and is stated in the spec rather than special-cased in the driver.

## Risks / Trade-offs

- [The block's numbers repeat each phase's own `Found <n> ...` line] → Deliberate: the block states the confirmation's scope before any work, the phase line states what that phase scanned. They report the same quantity, so the repetition reads as agreement rather than contradiction.
- [All subfolders are scanned before the first one starts] → The same work each subfolder would do at its turn; only the timing moves. The first console output is delayed by N scans instead of one, and a later subfolder's unreadable media now surfaces before the first subfolder runs — the outcome is unchanged (that subfolder is skipped).
- [A stop pending at start still prints the block, then the first subfolder reports the stop] → Harmless ordering; the stop path and exit codes are untouched.
- [Video counts can overstate the encoded total] → D5 and the spec's "counts are scan-time values" requirement; the wording never promises work, and the two causes (probe skips, already-encoded HEVC files) are named in D5.
- [`--pack-only` batches gain no scope block, so a long zip run still shows no up-front total] → Accepted non-goal: it needs a full-tree count that the presence-only pre-check exists to avoid. The pack-path defects recorded in proposal.md's Impact are where that belongs.
- [`--quiet` shows the prompt with no scope block above it] → D7 and the spec's quiet scenario: the flag's contract is "narration off, prompts intact".
- [Thirteen existing pre-check assertions move from a bool to a count] → Mechanical edit (`CHECK(x == 1)` / `CHECK(x == 0)`); the case names and their fixture setups stay as they are.
