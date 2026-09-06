## Why

Two first-run traps make the completion feature look broken to new users. First, the completion subcommand's help teaches `encro completion <powershell|bash> [--install | --uninstall]`, where `[--install | --uninstall]` reads like a verb that introduces the action and nothing signals that the shell argument is the primary operand — users reasonably type `encro completion --install powershell`, which works (the parser accepts both orders) but the help never says so, and the bare `--install` error message shows the opposite order. Second, PowerShell install wires only profile files that already exist, and when none exist creates only the PowerShell 7 profile — a user who then opens Windows PowerShell 5.1 (the Windows default) gets no completion and concludes the feature is broken; even on the right edition the current session never loads it.

## What Changes

- The completion subcommand help shows the flag-first canonical synopsis (`encro completion [--install | --uninstall] <powershell|bash>`) plus a concrete example line (`encro completion powershell --install`). Both argument orders keep working; only the documented form changes.
- The "specify a shell" error for install/uninstall without a shell shows the flag-first form.
- PowerShell install wires both known profile locations (Windows PowerShell 5.1 and PowerShell 7), creating a missing profile file when necessary, instead of only existing files (with a PS7-only fallback). A first-time install works in either PowerShell edition.
- Uninstall deletes a startup/profile file that is empty after the encro block is removed — whether install created it or it pre-existed empty — so no empty files are left behind; files with pre-existing content keep their content.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `shell-completion`: new requirement — the completion subcommand help teaches the flag-first canonical form with a concrete example.
- `completion-install`: install requirement now mandates wiring both PowerShell profile locations (creating missing ones); the missing-shell error shows the flag-first form; uninstall deletes startup files left empty after unwiring (provenance-free).

## Impact

- `src/cmd/cmd.cpp`: `kCompletionUsageLines` gains the flag-first synopsis and example line.
- `src/cmd/completion_command.cpp`: missing-shell error message reordered.
- `src/cmd/completion_install.cpp`: `installPowerShell` wires both profiles unconditionally (the `foundProfile` branch is deleted); bash/PowerShell uninstall deletes files left empty after block removal.
- Tests: `tests/cmd_completion_command_tests.cpp` (existing help-routing assertions pin the current synopsis substring and must be updated, plus the error-message test), `tests/cmd_completion_install_tests.cpp` (dual-profile wiring, empty-file cleanup).
- README examples keep the positional-first form; it remains valid, so no README change is required.
