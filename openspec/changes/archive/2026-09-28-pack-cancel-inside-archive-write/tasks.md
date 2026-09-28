## 1. Red-first tests

- [x] 1.1 Add a case to `tests/packer_tests.cpp` that drives the entries+callbacks overload with an `onEntryPacked` callback requesting a stop after the first entry, and asserts the call fails with `Packing canceled by user.` and leaves no archive at the output path. Verify red with `MSYSTEM= xmake test-report --tag="[packer]"`: today the write runs to completion, so the failure assertion goes red.
- [x] 1.2 Add a case to `tests/packer_tests.cpp` that requests a stop before the entries+progress overload runs and asserts it fails and leaves no archive. Verify red the same way.

## 2. Implementation

- [x] 2.1 In `src/pack/packer.cpp`, add the `infra/stop_signal.h` include and a `stopsignal::isStopRequested()` check at the head of each entry in both write loops; on a pending stop discard the open archive and return `Packing canceled by user.`. Verify both 1.1 and 1.2 go green.
- [x] 2.2 Verify the funnel is unchanged: the existing `[pack]` cases that pin the cancellation notice and the cancellation exit code stay green (`MSYSTEM= xmake test-report --tag="[pack]"`).

## 3. Verification

- [x] 3.1 Run the full suite: `MSYSTEM= xmake test-report` reports zero failures. (16384 assertions, 0 failures)
- [x] 3.2 Run the e2e suite: `MSYSTEM= xmake build e2e_tests && MSYSTEM= xmake run e2e_tests` passes. (831 assertions, 54 cases)
- [x] 3.3 Run `MSYSTEM= xmake fmt` twice and confirm the second run leaves no diff. (second run byte-identical; the only diff was pre-existing drift in two unrelated files, reverted)
- [x] 3.4 Run `MSYSTEM= xmake tidy` and compare the diagnostic count against the pre-change count recorded before implementing; no new diagnostic. (132 before, 132 after; the two `packer.cpp` diagnostics only shifted lines)
- [x] 3.5 Run the code-review skill's code stage over the change's diff (Standards / Spec / Leanness) and record the findings and verdicts in this file.

## 5. Code review (code stage)

## 4. Archive

- [x] 4.1 Archive the change with the spec sync, then verify `openspec/specs/cancellation-reporting/spec.md` carries the mid-archive clause and the change sits under `openspec/changes/archive/`. (synced and validated; archived as `2026-09-28-pack-cancel-inside-archive-write`)

## 5. Code review (code stage)

Reviewed the diff since `c9bc9fd` on three axes; fixes in `75c4cad`, verdicts from a fresh verifier, plus the follow-up it found.

### Standards

- Rejected: the four-line guard is duplicated in both write loops; a shared helper returns `bool` and still needs the same guard and return at both call sites, so it nets no line, and the leanness axis found nothing to cut.
- Rejected: `Packing canceled by user.` now appears at five sites; the packer must return the funnel's exact string, the other three sites predate this change, and the repo has no message-constant convention.
- Resolved (`75c4cad`): the packer cases' leading comment now states behaviour instead of where the check sits.
- Accepted non-goal (design, Non-Goals): a stop that lands after the last entry, during `zip.close()`, is not a checkpoint.

### Spec

- Resolved (`75c4cad`): the mid-write abort's funnel outcome was unpinned; `tests/pack_execute_tests.cpp` now drives the stop from `onCompactProgress` and asserts exit 130, no zipped files, no archive.
- Accepted non-goal: the last-entry/`close()` window above.
- Resolved (`75c4cad`): `discard()` does not delete the file - libzip materialises the archive at close - so the packer cases now also pin that a pre-existing archive survives the abort intact, the packer comment states the real mechanism, and design D2 was corrected. `unlink()` rejected: it would delete a previous run's archive, which the spec's clause does not ask for.
- Resolved (`20d368f`): `proposal.md` still claimed two packer cases and no other test file; found by the verifier.

### Leanness

- No findings: nothing reinvented from the standard library, no new dependency, no abstraction, config or dead flexibility added.
