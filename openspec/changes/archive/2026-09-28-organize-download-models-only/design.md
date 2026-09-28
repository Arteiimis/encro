## Context

See `proposal.md` — Why. The state that shapes the approach:

- `CmdParseResult::organizeDir` is already `std::optional<std::string>` (`src/cmd/cmd.h:62`), but the positional is registered as `cfg::Required{}` (`src/cmd/cmd.cpp:33`), which is `option->required()` (`src/cmd/option_specs.h:96`). CLI11 therefore rejects the invocation before any command code runs, and the app layer prints it as `error: Invalid arguments: dir is required` plus the help hint (`src/app/app_entry.cpp:130`).
- The command layer already tolerates an absent directory in a way that makes simply dropping `required()` wrong: `Options::root` falls back with `cmd.organizeDir.value_or(".")` (`src/organize/organize_command.cpp:143`), so a bare `encro organize` would silently organize the current directory instead of reporting the missing argument.
- `--download-models` runs `downloadModels(modelDir)` (`src/organize/organize_command.cpp:129`) and then falls through to the presence check (`:132`) and the normal flow; `tagger::fakeTaggerRequested()` bypasses model management entirely, which is the seam the e2e tests use.
- The two model checks ask different questions: `downloadMissing` skips any file that already exists (`src/tagger/model_store.cpp:293`) while `missingFiles` also compares the pinned size (`:154-160`), so a present file of the wrong size is caught by the presence check alone. That is why the fetch-only form cannot skip it.
- A post-parse, subcommand-scoped argument rule that keeps the CLI's own error contract has a precedent: `configActionArityError` (`src/cmd/cmd.cpp:161`) is called immediately after the subcommand match (`:540-542`) and writes into `result.error`.
- The organize usage lines are static strings (`kOrganizeUsageLines`, `src/cmd/help_layout.cpp:519`).

## Goals / Non-Goals

**Goals:**

- The fetch-only invocation works with no directory, and it is distinguishable from a run: it scans nothing and copies nothing.
- The missing-directory case keeps today's exact message (`dir is required`), hint and exit code — the argument becomes conditional, not weaker.
- `encro organize <dir> --download-models` behaves exactly as it does today.

**Non-Goals:**

- A separate `models`/`download` subcommand or any second place to fetch the models.
- Changing what is downloaded, verified or installed, or the cuDNN step's conditions.
- Making the directory optional for any other subcommand, or defaulting a bare `encro organize` to the current directory.
- A "models already present, nothing to do" message: the fetch header already names the directory, and the run exits 0.

## Decisions

**D1 — The requirement lives in the cmd layer, as a conditional rule beside `configActionArityError`.** `cfg::Required{}` comes off the `dir` positional and a file-local `organizeDirError(CmdParseResult const&) -> std::optional<std::string>` returns `dir is required` when the organize subcommand matched with neither a directory nor `--download-models`; the post-parse block writes it into `result.error`. Alternatives considered: (a) a generic `cfg::RequiredUnless{}` token in `option_specs.h` — speculative generality for the one option that needs it, and it would have to reach across into another option's presence anyway; (b) enforcing in the command layer — that layer returns exit codes, not parse errors, so it would have to reproduce the app layer's `Invalid arguments:` line and the `-h` hint, splitting one CLI contract across two layers and changing the message users see today; (c) keeping `required()` and letting CLI11 express the condition — CLI11 has no conditional-required hook, and `require_option(min, max)` (`CLI/App.hpp:844`) counts how many of the subcommand's options were used and reports that count, naming no argument, which is a worse message than the one users get today.

**D2 — The fetch-only run returns after the presence check, before the engines and the scan.** In `runOrganizeCommand`, an absent directory means the flag was the whole request, so the function downloads what is missing, runs the presence check (a file that exists with the wrong size is skipped by the downloader at `src/tagger/model_store.cpp:293` and caught only by `missingFiles` at `:154-160`), and then returns 0 where a run would build the engines and scan. `Options::root`'s `value_or(".")` fallback stays: hand-built `CmdParseResult`s in the pipeline tests set the directory explicitly, and the parse rule guarantees one whenever a real run reaches the scan. Alternative considered: returning before the presence check on the grounds that `downloadMissing` already guarantees completeness — wrong, because it only re-fetches missing files, so a truncated or wrong-size model would make the fetch-only form exit 0 while the `<dir>` form exits 1 with `models not found in ...`.

**D3 — The usage line and the flag's help state the fetch-only form.** `kOrganizeUsageLines` becomes `encro organize [dir] ...`, and `--download-models` says it fetches and, without a directory, stops there. The help is the only place a user can learn that a flag may stand alone; the alternative — leaving both silent — is how the argument came to look mandatory.

## Risks / Trade-offs

- **A fetch-only run that finds every file present prints only the header line** (the downloader reports what it does) → it still names the model directory and exits 0; a "nothing to do" line is deliberately out of scope.
- **A user may expect `encro organize` to mean "organize the current directory"** → unchanged from today: it is an argument error, and only the `--download-models` form is accepted without a directory.
- **`encro organize --download-models` has no automated end-to-end observer of the download itself** → `ENCRO_FAKE_TAGGER` skips model management and the pinned files are 530 MB, so no test can serve them. The new CLI case therefore asserts only that the form is accepted and does nothing else (exit 0 plus no `organized/` tree under the child's working directory, which is the tree a silent fallback to `.` would create; it cannot assert the directory line, which only `downloadModels` prints and the fake seam skips). What the flag fetches is covered one level down: `downloadFile`'s checksum verification, bounded retry and mirror fallback against a loopback server, and `missingFiles`/`allFilesPresent`'s size rule over a plain temporary directory, both in the `[tagger]` cases — while `downloadMissing`'s composition of those is covered by no test, and task 3.2's manual run against the real model directory exercises the "nothing missing" path only. The delta's "fetched" clause is the flag's pre-existing behavior, unchanged here; the change's own promise (the fetch-only shape) is what the CLI case observes, and its no-scan half is verified by mutation (task 3.3).
- **Two places now describe the directory's requirement** (the parse rule and the command's return path) → they answer different questions (may the invocation proceed; is there anything to organize), and D2's comment names the split at the point it happens.

## Migration Plan

None: no persisted state, no configuration, no on-disk artifact. The CLI only accepts more input than before, and rollback is reverting the implementation commit.
