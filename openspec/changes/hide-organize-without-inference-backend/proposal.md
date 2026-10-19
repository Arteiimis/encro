# Proposal

## Why

`encro organize` needs a local inference backend, and only the Windows build links one (the `onnxruntime-gpu` package ships win-x64 binaries). Elsewhere the command is compiled against stub engines whose `classify`/`extract` always fail, so a run scans the folder, files every image into `organized/uncategorized/`, and exits 0 — it reports success while doing nothing a user asked for. The command is still listed in `-h`, offered by shell completion, and accepted by the parser, and `--download-models` will happily pull ~530 MB for a backend that cannot use it.

Hiding the command until a POSIX backend exists makes the build honest: what the help offers is what this build can do. The catch is that the fake-tagger seam (`ENCRO_FAKE_TAGGER`) drives the whole organize pipeline on any platform with a fixture, and the Linux-only CI is where that coverage lives — so "unavailable" must mean "no backend at all", not "not Windows".

## What Changes

- **One availability rule.** organize is available when the build links the ONNX engines (Windows today) or when `ENCRO_FAKE_TAGGER` is set, which is a real backend as far as the pipeline is concerned. Everything below keys off that one rule.
- **Hidden when unavailable.** The `encro commands:` section of `-h`/`-hh` and the generated completion scripts omit `organize` in a build without a backend. `encro organize` stays a *registered* subcommand (its own `-h`, its options and its `model-dir`/`identity-tau` config keys keep working), so the parser and the config-key table are unchanged and the config file stays portable across platforms.
- **Refused when unavailable, before any work.** `encro organize <dir>` on a backend-less build exits non-zero with an error naming the missing inference backend — before the scan, before any copy, and before `--download-models` can touch the network.
- **BREAKING (exit code).** On a backend-less build `encro organize` used to exit 0 after filing everything under `uncategorized/`; it now exits non-zero without filing anything. Linux scripts that read that exit code (or that expected the `uncategorized/` tree) break. No file is removed or renamed by the new behaviour.
- **Docs follow the behaviour.** The README platform table and the public-API command table say organize is Windows-only until a POSIX backend exists, instead of "runs but cannot sort".

## Capabilities

### New Capabilities

None. Availability is a property of the existing `organize` command, not a new system behaviour.

### Modified Capabilities

- `image-character-organize`: adds the availability requirement — when the command may run at all, what happens to an unavailable invocation, and that the fake backend counts as available; no existing requirement is rewritten, because with the command refused there is no run for the copy-and-classify requirements to describe.
- `cli-help-layout`: the main-help commands section lists the commands this build offers, not every registered subcommand.
- `shell-completion`: generated scripts complete the same subcommand set the help shows.

## Impact

- Code: `src/tagger/engine_factory.{h,cpp}` (the availability predicate lives with the engines it describes), `src/cmd/cmd.cpp` (hidden marker at registration), `src/cmd/help_layout.cpp` (commands-section filter), `src/cmd/completion_emitter.cpp` (scope filter), `src/organize/organize_command.cpp` (the refusal guard), `src/app/app_entry.cpp` unchanged.
- Tests: the visibility assertions in `tests/cmd_cmd_tests.cpp`, `tests/cmd_organize_tests.cpp` and `tests/cmd_completion_emitter_tests.cpp` gain the availability dimension; `tests/e2e/encro_organize_tests.cpp` gets the refusal case and the fake-backed runs keep exercising the pipeline on Linux, plus one case for "hidden in help, absent from completion"; the opt-in `tests/cmd_completion_smoke_tests.cpp` native-delegation probe keeps covering `organize --model-dir` under the fake. `tests/organize/**` are untouched (they call the module directly).
- Docs: `README.md` "What each build can do", `docs/public-api.md` command table plus a new availability note (that file carries no platform note today).
- Unchanged on purpose: the config key set (`model-dir`, `identity-tau` stay accepted everywhere), the organize help text, the pipeline, the cache format, and every dependency. No new files on disk.
