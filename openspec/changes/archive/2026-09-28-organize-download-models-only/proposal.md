## Why

`encro organize --download-models` is a complete request on its own — fetching the model files needs no images — yet it dies in argument validation with `error: Invalid arguments: dir is required`. The models are a one-time ~530 MB provisioning step and this flag is the only way to fetch them, so today the user has to invent a directory to type, and then gets an organize run they did not ask for.

The argument is required at the CLI layer even though the parsed field is already optional (`std::optional<std::string> organizeDir`), so the requirement is a registration detail rather than a decision the command makes.

## What Changes

- The directory becomes conditional: `encro organize --download-models` provisions the model files and exits 0 without scanning or copying anything.
- `encro organize` with neither a directory nor `--download-models` keeps failing with the same `Invalid arguments: dir is required` line and the help hint, exit non-zero — the argument is required, not dropped.
- `encro organize <dir> --download-models` keeps today's meaning: fetch what is missing, then run.
- The organize usage line reads `encro organize [dir] ...`, and the `--download-models` help text states the fetch-only form.
- Nothing about what `--download-models` downloads, verifies or installs changes.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `image-character-organize`: command surface and scanning (the directory is optional with `--download-models`, and the fetch-only form scans nothing).

## Impact

- `src/cmd/cmd.cpp` — the `dir` positional stops being CLI-level required, and a post-parse rule beside `configActionArityError` keeps the argument error for the no-flag case; the `--download-models` help text gains the fetch-only form.
- `src/cmd/help_layout.cpp` — the organize usage line (`kOrganizeUsageLines`).
- `src/organize/organize_command.cpp` — a fetch-only branch that returns 0 after the model-presence check, before the engines and the scan.
- Tests: `tests/cmd_organize_tests.cpp` (parse-level), `tests/e2e/encro_organize_tests.cpp` (CLI-level).
