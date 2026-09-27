## 1. Red-first tests

- [ ] 1.1 Add a case to `tests/packer_tests.cpp` that drives the entries+callbacks overload with an `onEntryPacked` callback requesting a stop after the first entry, and asserts the call fails with `Packing canceled by user.` and leaves no archive at the output path. Verify red with `MSYSTEM= xmake test-report --tag="[packer]"`: today the write runs to completion, so the failure assertion goes red.
- [ ] 1.2 Add a case to `tests/packer_tests.cpp` that requests a stop before the entries+progress overload runs and asserts it fails and leaves no archive. Verify red the same way.

## 2. Implementation

- [ ] 2.1 In `src/pack/packer.cpp`, add the `infra/stop_signal.h` include and a `stopsignal::isStopRequested()` check at the head of each entry in both write loops; on a pending stop discard the open archive and return `Packing canceled by user.`. Verify both 1.1 and 1.2 go green.
- [ ] 2.2 Verify the funnel is unchanged: the existing `[pack]` cases that pin the cancellation notice and the cancellation exit code stay green (`MSYSTEM= xmake test-report --tag="[pack]"`).

## 3. Verification

- [ ] 3.1 Run the full suite: `MSYSTEM= xmake test-report` reports zero failures.
- [ ] 3.2 Run the e2e suite: `MSYSTEM= xmake build e2e_tests && MSYSTEM= xmake run e2e_tests` passes.
- [ ] 3.3 Run `MSYSTEM= xmake fmt` twice and confirm the second run leaves no diff.
- [ ] 3.4 Run `MSYSTEM= xmake tidy` and compare the diagnostic count against the pre-change count recorded before implementing; no new diagnostic.
- [ ] 3.5 Run the code-review skill's code stage over the change's diff (Standards / Spec / Leanness) and record the findings and verdicts in this file.

## 4. Archive

- [ ] 4.1 Archive the change with the spec sync, then verify `openspec/specs/cancellation-reporting/spec.md` carries the mid-archive clause and the change sits under `openspec/changes/archive/`.
