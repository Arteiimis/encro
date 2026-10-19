# Proposal

## Why

`xmake size` answers "how big is this binary", and it is the one developer task that reads a binary the running process is *not* configured for. Two defects make it answer about the wrong binary. Its `--mode` option defaults to the literal `release` instead of the mode the project is configured for — a leftover of the removed PDB-rebuild branch — so in a `debug` tree `xmake size` measures a binary the caller did not just build, or fails with "binary not found" for a mode that was never built. And the hand-joined path takes its architecture from `os.arch()` (the host) while every path xmake reports takes it from `config.arch()` (the configured architecture), so a tree configured for another architecture looks in a directory that does not exist.

`docs/backlog.md` records both symptoms. Probing the API corrected the recorded cause: `target:targetfile()` *does* follow a mode set in memory (`config.set("mode", "coverage")` moved it from `build\windows\x64\release\encro.exe` to `build\windows\x64\coverage\encro.exe`), so the blocker was never the API — it is the option's default value, which the earlier entry mistook for an architectural limit. That correction is what makes the fix three lines instead of a CLI redesign.

## What Changes

- `--mode` defaults to the configured build mode, so `xmake size` measures the binary the caller built, while `xmake size --mode=<other>` still measures another mode's binary on purpose.
- The binary path takes its architecture from `config.arch() or os.arch()`, so a configured architecture is honoured.
- The option's help text names the new default.

Not changed: the hand-joined `builddir/<plat>/<arch>/<mode>/<target><ext>` path itself, the `--target`, `--top` and `--breakdown` options, and the PDB-based per-object breakdown. The alternative the backlog suggested — delete `--mode` and require `xmake f -m <mode>` first — is rejected in `design.md`: it removes a working capability to fix a default.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None — this change sets `skip_specs: true`. The only capabilities that own developer tasks are `test-task` and `coverage-report`; the size task has no requirement of its own, and this change moves a default value and one path segment rather than establishing a behavioural contract. Recording it would mean inventing a `size-report` capability for a three-line fix, and the previous change to the same file (`simplify-xmake-build-scripts`) deliberately did not create one either. Verification is by running the task and reading its output (`tasks.md`), which is how the other plugin tasks are checked — plugin Lua has no test harness in this repository.

## Impact

- `plugins/size/xmake.lua` only: no other code, no dependency, no build-system or CI change.
- Behaviour: `xmake size` with no options now analyses the configured mode instead of `release`. In a release-configured tree — the default, and what the release flow uses — nothing changes; CI builds other modes but never runs this task. A caller that wants `release` from a non-release tree must pass `--mode=release`.
- `docs/backlog.md`: the entry that recorded this is resolved when the change lands.
- Nothing in the repository calls `xmake size`: no CI workflow, script or doc depends on the old default.
