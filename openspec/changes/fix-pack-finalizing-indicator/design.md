## Context

See `proposal.md` — Why for the two defects. Constraints that shape the fix:

- The compact path builds one `CompactProgressState` per run (`src/pack/pack_service.cpp`): a single bar, a `finalizingCount` spanning all in-flight archive tasks, a spinner thread that currently owns or yields the postfix slot depending on that counter.
- `ProgressContext` renders every setter call immediately (`setPostfixText`/`setProgress` → `DynamicProgress::print_progress`, a full bar-line repaint) and additionally repaints from its own 100 ms ticker (`src/core/progress.cpp`). Nothing is coalesced or rate-limited.
- The `--full-progress` path (`src/pack/packer.cpp`) already composes `label | Finalizing <frame>` per archive bar and sleeps 120 ms inside its loop; it is the reference shape for the text and needs no change.
- Archive trailers are written by `zip.close()` between the counter increment and decrement (`src/pack/packer.cpp:518-522`); that interval is the finalizing window, and with test-sized archives it lasts microseconds — it is not observable by a fixed-cadence indicator unless a test holds it open.
- `onCompactStatusText` is the only status channel that exists in tests (stdout is not a terminal there); production installs no consumer.

## Goals / Non-Goals

**Goals:**
- One owner for the compact line's text: the packing label is always present, the finalizing indicator is an appended ` | ` part.
- Frame stepping decoupled from publish rate, with the publish cadence bounded by the 120 ms frame interval.
- No indicator activity when nothing can display it.

**Non-Goals:**
- Changing the full-progress packing path, the postfix ` | ` grammar, the scroll window rules, packing grouping/naming/exit codes/job-state semantics, or CLI surface.
- Coalescing or rate-limiting progress rendering in `ProgressContext` in general.

## Decisions

### D1: Compose one postfix from a stored label plus an optional finalizing part

`CompactProgressState` keeps the current packing label; a single `publish()` composes `label` (unchanged) or `label | Finalizing <frame>` (while `finalizingCount > 0`) and writes it to the bar and to the status-text consumer. Packing updates replace the label and publish; the spinner only publishes. The `finalizingCount == 0` suppression guard inside the packing-update path is deleted — nobody is suppressed, so no writer can erase another's content.

Alternatives rejected: (a) keep both writers and only restore the cadence — the flip remains, up to 8 text swaps/s; (b) show the standalone `Finalizing <frame>` text only once every entry is packed (smallest change, but the indicator disappears from every mid-run finalizing window and the label is still replaced in the tail); (c) append a count of archives currently finalizing (`| finalizing 2`) — more information, but diverges from the full-progress grammar and needs extra composition state.

### D2: Frame is a function of elapsed time

`frame = kFrames[(elapsedMs / 120) % 4]`, computed at publish time from the steady clock — the same time-derived pattern the postfix scroller already uses (`bounceOffset`). A repaint inside a frame interval therefore cannot change the frame, and repaint rate cannot change animation speed.

Alternatives rejected: a per-iteration counter plus a guaranteed wait fixes today's symptom but leaves the defect class: any future refactor that drops the wait restores "spins as fast as it repaints", and any second writer advances the frame at the progress-update rate.

### D3: Cadence enforced by loop shape, not by a counter

The spinner waits 120 ms in every iteration, including iterations that render (this is the pre-`7d8c626` shape), while keeping the existing wake-on-stop wait so a stop request does not add latency. The shared 100 ms repaint ticker keeps running: it repaints the stored postfix — the composed text — without publishing to the status consumer, and in a non-renderable run it returns early after counting the wakeup. The spec's bound therefore counts frame changes, not status texts: every publish during a finalizing window carries the current frame, so several publishes can share one frame, and the ticker can repaint the identical composed text at its own cadence.

Alternative rejected: drive the frame from the shared ticker and delete the spinner thread — fewer threads, but it requires a per-bar animated tail in `ProgressContext`, i.e. new API surface in the shared component whose repaint behaviour is already spec-governed (`progress-scroll-label`), for no user-visible gain.

### D4: Indicator runs only where it can be seen

A new `ProgressContext::renderable()` (bars added and bars allowed: stdout is a terminal and output is not quiet) replaces the anonymous-namespace predicate for this purpose. The spinner starts only when `renderable()` or a status-text consumer is installed. In production that means interactive runs only; in tests, the consumer keeps the indicator observable, which is also what the spec's "status consumer alone keeps the indicator alive" scenario pins.

Alternatives rejected: always start the thread (status quo: a timing thread for output nobody can receive); gate on the bar index alone (it is always present in compact mode, so it decides nothing).

### D5: Finalizing window is opened for tests through a plan-carried hook

`PackPlan` gains an optional `onBeforeArchiveClose` callback, forwarded into the archive close step and invoked after the counter increment, before `zip.close()`. Production callers leave it null; `selectPackPlanIndexes` forwards it like the other plan fields. Tests install a gate that holds the window open, which is what makes both new assertions possible (label survival, cadence bound) and makes the old code fail them.

Alternatives rejected: (a) no seam — with a 120 ms cadence and microsecond-scale test archives the indicator emits ~0 frames, so the tests would pass against the broken code; (b) gate through an environment variable, i.e. production code reading test configuration; (c) rely on real archive sizes for a long close — machine-dependent and slow.

### D6: No new CLI, config, or doc surface

The line's text grammar is unchanged (`Packing: archive k/N [file n/m]`, `Packed: archive N/N complete`, ` | ` separator), so nothing in the user-facing docs changes.

## Risks / Trade-offs

- **Test-only hook in a production header** → documented as a seam, null by default, single call site; behaves as a no-op when unset.
- **Two publishers on one bar (spinner and the 100 ms ticker)** → both go through the same composer under the same state mutex, so no torn or contradictory text; the ticker only repaints the composed text and never publishes to the consumer, so the spec's frame-change bound stays measurable.
- **Frame boundary aliasing** (a repaint landing exactly on a 120 ms boundary can show one frame for two boundaries, or skip one under load) → cosmetic, accepted; the cadence bound in the spec is expressed with a `+1` slack for it.
- **A status-text consumer that blocks stalls packing progress** (the callback runs under the state mutex) → pre-existing behaviour; the new tests use the gate, not this callback, for blocking.
- **Verification limit (accepted)**: the rule "no indicator without a destination" is verified through its inputs (`renderable()` is false on a non-TTY stdout) and its positive direction (a status-text consumer alone keeps the indicator alive); the thread's absence itself is deliberately not asserted, because observing it would need thread introspection or production instrumentation that exists only for the test. The `!bars_.empty()` half of `renderable()` is inert at its only call site (the compact path adds its bar before starting the indicator) and cannot be exercised at all without a real terminal, so no test isolates it.
