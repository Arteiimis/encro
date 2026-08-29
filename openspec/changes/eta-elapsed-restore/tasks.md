## 1. Elapsed clock in the estimator (TDD)

- [ ] 1.1 Add failing tests: `EtaEstimator::elapsedSeconds()` returns nullopt before the first positive-progress sample, grows with simulated time after anchoring, honors an elapsed base offset, and returns nullopt after `reset()`
- [ ] 1.2 Implement `elapsedBaseSec_`, `elapsedSeconds()`, and the `elapsedBaseSec` parameter on `reset()`; watch 1.1 pass

## 2. Badge formatting (TDD)

- [ ] 2.1 Add failing tests for the exported pure `formatEtaBadge(elapsedSec, etaSec)`: both values render `[12m:34s/1h:24m]`; nullopt estimate renders `--:--` placeholder; nullopt elapsed yields no badge; granularity switches per part at one hour; ceiling rounding keeps a running encode off `0m:00s`
- [ ] 2.2 Implement `formatEtaBadge` (replacing `formatEtaPart`) and wire it into `applyBarText` with the visibility gates (no badge before anchor, no badge at 100 percent); watch 2.1 pass
- [ ] 2.3 Extend `fitPostfixWithEta` tests with a badge-width case and confirm the pinned-prefix requirement still holds

## 3. Persisted accumulation in the job state store (TDD)

- [ ] 3.1 Add failing tests: `encodedMs` round-trips through save/load; `markProgress` settles `encodedMs += now - startedAtMs`; `markInterrupted`/`markFailed`/`markSucceeded` settle exactly; `clearExecutionState` resets it; a state file written without the field loads as 0
- [ ] 3.2 Add `encodedMs` to `TaskRecord`, JSON serialization, and the settlement logic; watch 3.1 pass

## 4. Wiring restore into the encode flow (TDD)

- [ ] 4.1 Add a failing test that `barEncodingStart` seeds the bar's elapsed clock from the task record's accumulated time (resume scenario: interrupted attempt persisted 3h, next attempt shows 3h+delta)
- [ ] 4.2 Implement the lookup in `runEncodingTask`/`barEncodingStart` (action id -> `encodedMs`) and pass it through `resetEta`; watch 4.1 pass

## 5. Verification

- [ ] 5.1 Run the full unit suite (`xmake test-report`) and the e2e suite; fix any regressions
- [ ] 5.2 Run `xmake fmt` and the pre-commit hook; commit implementation + tests + checked tasks in one commit
- [ ] 5.3 Manual smoke on a real file: badge shows `[elapsed/--:--]` during the seeding window and `[elapsed/estimate]` after; interrupt + resume shows accumulated elapsed
