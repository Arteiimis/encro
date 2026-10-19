# Design

## Context

`plugins/size/xmake.lua` is a task plugin: `on_run` loads the project configuration, computes one binary path from it, and hands that path to `llvm-size` (section overview) and `llvm-pdbutil` (per-object breakdown). See `proposal.md` - Why for the motivation; this section records only the constraints that decide the shape of the fix.

- The task must be able to read a binary built in a mode the running process is **not** configured for — that is the whole point of `--mode`. A child `xmake f -m …` does not refresh this process's configuration or its loaded targets, and neither `config.unload` nor `project.unload` exists, so the path cannot be obtained by reconfiguring and asking.
- `config.load(filepath, opt)` merges the on-disk configuration into memory and takes no value overrides, so there is no supported way to *load* a different mode either.
- `target:targetfile()` is evaluated against the configuration in memory at call time: probed 2026-10-19, `config.set("mode", "coverage")` moved the answer from `build\windows\x64\release\encro.exe` to `build\windows\x64\coverage\encro.exe`, and `config.set("arch", "arm64")` moved it to `build\windows\arm64\release\encro.exe`, while `os.arch()` stayed `x64`.
- `option.get("mode")` returns the menu default when the flag is absent, so a default that depends on configuration cannot live in the menu table.
- The tree cannot be configured for another architecture: `xmake f -m release -a arm64 -y` fails while resolving the vendored `onnxruntime-gpu` package (`build-repo/packages/o/onnxruntime-gpu`, x64-scoped). A non-host architecture can therefore only be exercised in memory, by setting it before the task runs.

## Goals / Non-Goals

**Goals:**

- `xmake size` with no options analyses the binary the caller is building, in every build mode.
- The path honours the configured architecture.

**Non-Goals:**

- Removing or renaming `--mode`; changing `--target`, `--top` or `--breakdown`; touching the per-object breakdown.
- `plugins/coverage/xmake.lua`'s own `os.arch()` join, which is self-consistent: it reconfigures with `-c -p <platform>`, so its coverage-mode build is host-arch by construction.

## Decisions

**Default the mode in `on_run`, not in the menu.** The menu entry becomes `{"m", "mode", "kv", nil, "Build mode to analyze (default: the configured mode)"}` and `on_run` resolves `option.get("mode") or config.get("mode") or "release"`. Rationale: the menu table is static, the configured mode is only readable after `config.load()`; keeping `"release"` in the table would make "flag absent" and "flag given as release" indistinguishable. The final `or "release"` covers an unconfigured tree, where `config.get("mode")` is nil.

**Take the architecture from the configuration.** `config.arch() or os.arch()` replaces `os.arch()` in the path join. Rationale: the build paths that decide where a binary lands — the target's own `targetfile()` and every path xmake itself reports — key the directory on the configured architecture, so the host architecture was only ever right by coincidence.

**Keep the hand-joined path.** Alternatives considered: (a) `path.absolute(project.target(target):targetfile())` — correct only for the configured mode, so it would need a second code path for `--mode=<other>`; (b) `config.set("mode", mode)`, then `targetfile()`, then restore — removes the join but mutates process-wide configuration inside a task that also raises on a missing binary, which is a worse failure mode than a one-line join that computes the same form of path the target itself computes. For this repository's binary targets the join computes what `targetfile()` computes (`builddir/platform/arch/mode/name.ext`); it would diverge for a target that overrides its output directory or file name, and none does.

**Keep `--mode`.** The backlog suggested deleting it and requiring `xmake f -m <mode>` first. That trades a working option (it reads another mode's binary, which is what the option says) for a default-value fix, and it would make a two-mode comparison a configure-and-reconfigure dance. The default change alone removes the surprise the entry recorded.

## Risks / Trade-offs

- [A caller in a non-release tree relied on `xmake size` analysing `release`] → Pass `--mode=release`; nothing in the repository calls the task (no CI workflow, script or doc), so the exposure is a human habit, not an integration.
- [The join silently diverges if a target ever sets `set_targetdir()`] → No target does today; if one is added, switch that resolution to `targetfile()` with a scoped mode override, and say so in the plugin. The comment above the join names the constraint.
- [`--target` is not validated, and a non-binary target cannot produce a matching name] → `xmake size --target=encro_core` would look for `encro_core.exe` where the static library is `encro_core.lib`; the option exists to point the task at a binary and its default is `encro`, so this stays a documented characteristic of the join rather than a new validation path inside a task.
- [`config.arch()` can be nil on an unconfigured tree] → `or os.arch()` keeps the previous behaviour there instead of raising.
- [A non-host architecture cannot be configured, so the architecture fix has no configured-tree check] → Verified by overriding the architecture in memory and running the task (`config.set("arch", "arm64")` then `task.run("size")` reports the `arm64` directory while `os.arch()` stays `x64`); a configured-tree check would need the vendored `onnxruntime-gpu` package to support that architecture.
