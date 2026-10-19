# Design

## Context

See `proposal.md` for motivation. The constraints that shape the approach:

- **The backend is a build fact.** `xmake.lua:106-110` requires `onnxruntime-gpu` only on Windows and `:166-171` removes `src/tagger/onnx_*.cpp` elsewhere; `engine_factory.cpp:158-200` then substitutes `UnsupportedPlatformTagger`/`UnsupportedPlatformFeatures`, whose `classify`/`extract` always error. `ENCRO_FAKE_TAGGER` short-circuits both factories with fixture engines on every platform.
- **Visibility has three independent surfaces.** The main help's `encro commands:` section renders from an explicit span passed by `buildAppTree` (`help_layout.cpp:347-388`, span built at `cmd.cpp:857-873`); the completion model walks `app->get_subcommands()` at emission time (`completion_emitter.cpp:177-190`); parsing/dispatch uses the registered app (`cmd.cpp:849`, `app_entry.cpp:314`).
- **The config-key table is coupled to registration.** `model-dir` and `identity-tau` are registered on the organize subcommand (`cmd.cpp:54-73`) and `assembleKeyTable` fails the whole run when the canonical order names a key no option registered (`config_store.cpp:55-88`).
- **CI is Linux-only** (`.github/workflows/ci.yml`), and organize's CLI/e2e coverage there runs entirely on `ENCRO_FAKE_TAGGER`.

## Goals / Non-Goals

**Goals:**

- One availability rule, consumed by all three visibility/dispatch surfaces, so help, completion and behaviour cannot disagree.
- Zero behaviour change on a build with the local engines; zero test-coverage loss on Linux CI.
- The refusal happens before any filesystem or network side effect.

**Non-Goals:**

- Shipping a POSIX inference backend (a Linux onnxruntime package, CoreML, etc.).
- Deleting organize code, changing its pipeline, its cache format, or its option set.
- Generalizing hidden/visible subcommands for the other four commands, which have no platform split.

## Decisions

### D1 — the rule lives beside the engines and counts the fake

`tagger::inferenceAvailable()` returns `kLinksOnnxEngines || fakeTaggerRequested()`, declared in `tagger/engine_factory.h` with `kLinksOnnxEngines` a `constexpr` set by `#if defined(_WIN32)`.

Rationale: availability *is* "can the engine factory hand out real engines", so the rule belongs next to the factory and the refusal guard reads the same fact the factory branches on. Counting the fake is what keeps `tests/organize/**`, the organize e2e suite and the CLI cases meaningful on the only CI platform that exists.

Alternative rejected: a compile-time-only predicate. It reads cleaner in the help output but turns the eight organize e2e cases and most `tests/cmd_organize_tests.cpp` cases into dead weight on Linux, where no Windows job exists to take over.

### D2 — hidden is one CLI11 marker, filtered by the two consumers

At registration, an unavailable build marks the subcommand with an empty group (`organizeSub->group("")`). CLI11 gives named subcommands the group `SUBCOMMANDS` by default (`CLI/App.hpp:313` in cli11 v2.7.2, the version xmake resolves today) and its own formatter skips empty-group entries, so the marker is the library's own "not in help" convention rather than a new one. This repo renders help itself, so both consumers filter explicitly on `get_group().empty()`: `formatCommandsSection` skips such subcommands when computing the auto-fit width *and* when rendering rows, and `buildCompletionModel` skips them when assembling `model.subcommands`.

Rationale: one marker, two one-line filters, no name string comparison, and registration is untouched — so the config-key table, `encro organize -h`, and the parser keep working by construction.

Alternatives rejected: a name allowlist (repeats `"organize"` across four files); an `AppTree` flag threaded into both consumers (more plumbing than a filtered span and a filtered loop).

### D3 — the refusal is a run-time failure at the top of the command body

`runOrganizeCommand` starts with the availability check and returns 1 after `terminal::messageln(Error, ...)` naming the missing backend, in the same shape as the existing `requireModels` failure. The check sits **before** `--download-models` and **before** `requireModels`.

Rationale: the argument-error channel would print `Invalid arguments:` plus a help hint, which misdescribes a build limitation as a typo. Ordering matters for the message a Linux user actually sees: behind `requireModels` the run would first report "models not found" (or, with `--download-models`, pull ~530 MB) and never mention that this build cannot classify anything. Argument validation still runs before the guard — it includes the `--ingest`/`--ignore-folder` check, which reads the root's directory listing but no image — so "before any work" means before the scan, the copies and the network, not before the parser.

### D4 — config keys stay accepted everywhere

`model-dir` and `identity-tau` remain registered, valid, and listed by `encro config list` on every platform.

Rationale: D2 keeps the subcommand registered, so this costs nothing; the config file is shared by a user's Windows and POSIX machines, and rejecting two keys on one of them would break that portability for no gain. A hidden command is not a removed capability.

Alternative rejected: dropping the keys from the canonical order on backend-less builds — that forces a conditional `kCanonicalKeyOrder`, a `user-config` spec delta and new config-store cases, to hide two read-only lines.

### D5 — the command is hidden, never unregistered

`registerOrganizeSubcommand` still runs on every platform; only availability changes what the user sees and whether a run proceeds.

Rationale: unregistering collides with the key table (D4), and it degrades unknown-command handling — `encro organize <dir>` would be parsed as positional inputs, producing an "input does not exist" error about a directory named `organize`. It would also invalidate the seventeen `[cmd][organize]` parse cases and the eight organize e2e cases on Linux, the only place they run.

### D6 — tests declare which side of the predicate they are on

- CLI-level visibility cases (`main help lists organize`, the `[organize]` rows in `cmd_cmd_tests.cpp`, the completion-model scope case) set `ENCRO_FAKE_TAGGER` through the existing `testutils::ScopedEnvVar` and assert the visible behaviour on every platform.
- The negative cases — `organize` absent from help and completion, and the refusal message — call `inferenceAvailable()` first and `SKIP("an inference backend is present here")` when it is true, so they execute on Linux CI and stay honest on Windows instead of being deleted.
- `e2e_tests` reaches `encro_core` through its `encro` dependency — it already calls `core::sha256Hex` — so its refusal cases call `inferenceAvailable()` and `SKIP()` like the CLI cases. The one existing case with a platform-dependent expectation (`missing models fail fast with guidance`, which deliberately runs without the fake) `SKIP()`s where `inferenceAvailable()` is false — letting the refusal case own that outcome — and keeps asserting the models message where a backend exists. No `#if` enters the test tree.
- New e2e coverage: an unavailable run exits non-zero, names the backend, and creates no `organized/` tree; a fake-backed run keeps filing images exactly as before (the existing cases already prove this).

## Risks / Trade-offs

- **[The test seam leaks into UX]** A backend-less build launched with `ENCRO_FAKE_TAGGER` shows `organize` in help. Mitigation: that build genuinely can organize (against a fixture), the variable is an existing undocumented debug seam, and the alternative — a second override variable — buys cleaner-looking help at the cost of a knob nobody needs.
- **[`group("")` is a convention, not an API guarantee]** A future CLI11 upgrade could change default group handling. Mitigation: the two filters are explicit, commented, and locked by the help/completion cases; a regression fails those, not a silent drift.
- **[Already-installed completion scripts keep offering `organize`]** Emission is on demand, so a shell wired earlier keeps its old script. Mitigation: document `encro completion <shell> --install` as the refresh step; nothing else changes.
- **[Exit-code breakage on Linux]** Scripts that treated `organize`'s exit 0 plus an `uncategorized/` tree as success now fail without writing. Mitigation: proposal and `docs/public-api.md` name it a breaking behaviour change, and the run writes nothing rather than half a tree.
- **[The unavailable path cannot be exercised on Windows]** Mitigation: `SKIP()` keeps the case compiled and visible in both suites; Linux CI runs it for real.

## Migration Plan

No data migration: no on-disk format, cache version or config key changes. On release, users on POSIX re-run `encro completion <shell> --install` to drop the stale `organize` scope; on Windows nothing changes. Rolling back is reverting the commit — the marker and the guard disappear together, and no artifact was written by the refused path.

## Open Questions

None that would change the specs or the task breakdown. When a POSIX backend lands, `kLinksOnnxEngines` becomes a per-platform build fact (or is replaced by the package's own availability), which this design already localizes to one predicate.
