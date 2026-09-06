## Context

The completion subcommand help renders from `kCompletionUsageLines` (`src/cmd/cmd.cpp`) through `makeSubcommandHelpFormatter`, whose usage section already renders a multi-line span (`formatHelpPreamble`), so an example line needs no new help-layout mechanism. The missing-shell error is a fixed string in `runCompletionCommand` (`src/cmd/completion_command.cpp`). `installPowerShell` (`src/cmd/completion_install.cpp`) loops the two profile paths but skips ones whose file cannot be read (does not exist), and creates only the PowerShell 7 profile when neither exists; uninstall (`uninstallPowerShell`/`uninstallBash`) always writes the block-removed content back, leaving empty files behind when install created them.

## Goals / Non-Goals

- Goals: help and errors teach the flag-first canonical form with a concrete example; a first-time PowerShell install works in both editions; uninstall leaves no empty files it created.
- Non-Goals: no parser or grammar change — both argument orders already parse (verified end-to-end in sandboxed installs); no detection of "which PowerShell edition the user prefers"; no changes to the bash completion-detection strategy.

## Decisions

- **D1 — Documentation-only canonicalization.** Keep the `shell` positional and both accepted orders; change the synopsis line, add one example line, and reorder the error string. Alternative rejected: making `--install`/`--uninstall` consume the shell as their value — it breaks `encro completion <shell> --install`, splits one concept across two grammatical slots, and buys nothing the parser does not already allow.
- **D2 — Wire both PowerShell profiles unconditionally.** Treat a missing profile file as empty content and write it (the guarded splice block is the only content). The current only-existing/PS7-fallback politeness is exactly what produced "installed but my powershell.exe does not complete". Windows ships 5.1 by default and many users add 7; both entries are inert in the other edition, so wiring both is safe. This deletes the `foundProfile` branch — net less code.
- **D3 — Uninstall deletes files left empty.** After removing the delimited block, if the remaining content is empty (or whitespace-only), delete the file instead of writing it back. No provenance tracking: a pre-existing empty startup file has no content to preserve, so the rule is safe without remembering whether encro created it. Applies to both PowerShell profiles and the bash startup fallback. This narrows the guarded-editing requirement ("preserve all existing content, append or remove only the block"): the more-specific uninstall requirement governs for emptied files, and files with any other content are still preserved exactly.
- **D4 — Example line lives in the existing usage span.** `kCompletionUsageLines` gains a second entry; `formatHelpPreamble` renders it as another usage line. Alternative rejected: a dedicated Examples section — new formatter machinery for one line.

## Risks / Trade-offs

- [Users see a profile file appear where none existed] → The file contains only the marked encro block; uninstall deletes it again (D3); the install output names every wired file (asserted as filesystem state in tests; stdout wording rides the existing install-output posture of the current print clause).
- [Synopsis order and positional row could still disagree] → The example line pins the concrete form; no claim is made that flag-first is the only valid order (it is not).
- [Empty-file deletion touches pre-existing empty files] → Deleting an empty file preserves all content by definition; risk accepted, scenario covers it.

## Migration Plan

No persisted state changes format. Existing installs pick up dual-profile wiring on the next `--install` re-run (idempotent per the existing requirement); empty-file cleanup applies on the next uninstall.
