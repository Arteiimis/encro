## 1. Repaint clock in `ProgressContext` (test first)

- [x] 1.1 Add the read-only `tickCount()` counter and `etaSeconds()` estimate view (framed like the existing `elapsedSeconds` diagnostics accessor) plus the failing `[progress]` tests: the count is 0 before the first `addBar` and grows on its own within a second afterwards with no setter or `tick()` call (poll with `testutils::waitUntil`, no sleeps), it stops growing after `eraseBars()`, repaints leave the progress value and the estimate untouched while the elapsed clock keeps counting, and the existing non-TTY no-frames test stays green with the clock running — verify `xmake test-report --tag="[progress]"` reports the new cases failing (RED).
- [x] 1.2 Start a `std::jthread` with the first `addBar` that wakes every 100 ms and calls `tick()`, stop it in `eraseBars()` before `mtx_` is taken, and count wakeups before the `progressBarsAllowed()` short-circuit; declare `ticker_` after `mtx_` so member destruction joins the thread before the mutex — verify `xmake test-report --tag="[progress]"` is green (GREEN).
- [x] 1.3 Delete `kScrollTickInterval` and the encode monitor's `progress().tick()` block so no command owns its own repaint timer — verify `rg "kScrollTickInterval" src` returns nothing and `xmake test-report` is green.

## 2. Unify the postfix grammar on ` | `

- [x] 2.1 Add a `[progress]` test pinning the grammar: a probe-shaped postfix (`Probing: <long name> | CQ 20 scoring`) under a budget narrower than the text keeps the label part scrolling while the `| CQ 20 scoring` tail holds the same position in every frame — verify `xmake test-report --tag="[progress]"` is green.
- [x] 2.2 Convert the four probe/preview postfixes from `·` to ` | ` (`src/video/encode_probe.cpp`, `src/preview/preview_process.cpp`) so the CQ/sub-step status is a pinned tail — verify `rg "·" src` returns nothing and `xmake test-report --tag="[encode-probe]"` plus `--tag="[preview]"` are green.

## 3. Verification

- [x] 3.1 Run `xmake fmt -k`, `xmake build encro`, and `xmake test-report` — verify all pass, and `xmake tidy` reports no new findings in the touched files.
- [x] 3.2 Run `xmake test-parallel` (unit + e2e shards) — verify the Catch2 logs report no failures (parallel `proc:wait` statuses are unreliable).
- [ ] 3.3 Manual check (no PTY facility in tests, so bar frames stay unobservable to the automated suites): in a real terminal run a probe+encode batch whose filenames overflow the postfix budget, plus a pack run — verify the probe slot bars keep scrolling between probe steps, their status after ` | ` stays pinned, pack bars animate, and encode bars behave as before.
