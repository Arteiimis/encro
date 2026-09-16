# completion-install Specification

## Purpose

Turns the generated completion scripts into one-command setup: `encro completion <shell> --install` writes the script to a stable per-user location and wires it into the shell's startup, `--uninstall` reverses it completely, and both operations are idempotent and minimally invasive to the user's shell profiles.

## Requirements

### Requirement: Install activates completion for new shell sessions

`encro completion <shell> --install` SHALL write the completion script for the requested shell to a stable per-user location and register it so that new interactive sessions of that shell load the completion. For PowerShell this SHALL be done by referencing the script from both of the user's known PowerShell profile locations (Windows PowerShell 5.1 and PowerShell 7), creating a missing profile file when necessary, so a first-time install works in either PowerShell edition; for bash this SHALL be done via the bash-completion lazy-load location when the bash-completion framework is present, and via the user's bash startup file otherwise. On success the command SHALL print the installed script path and every profile file it wired. When install or uninstall is requested without a shell, the error message SHALL show the flag-first invocation form.

#### Scenario: PowerShell install wires a profile

- **WHEN** the user runs `encro completion powershell --install`
- **THEN** the script is written under the encro per-user directory and the command reports that path and every profile file it wired
- **AND** a new PowerShell session dot-sources the script through an entry in the profile

#### Scenario: First-time PowerShell install wires both editions

- **WHEN** the user runs `encro completion powershell --install` and neither PowerShell profile file exists
- **THEN** both the Windows PowerShell 5.1 profile and the PowerShell 7 profile are created with exactly one guarded encro entry each

#### Scenario: Missing shell names the flag-first form

- **WHEN** the user runs `encro completion --install` without a shell argument
- **THEN** the command fails and the error message shows the flag-first form (for example `encro completion --install <powershell|bash>`, matching the help's synopsis order)

#### Scenario: Bash install uses the lazy-load directory when bash-completion is present

- **WHEN** the user runs `encro completion bash --install` on a system where the bash-completion framework is installed
- **THEN** the script is written directly into the user's bash-completion completions directory as `encro`
- **AND** the bash startup file is left unmodified

#### Scenario: Bash install falls back to the startup file

- **WHEN** the user runs `encro completion bash --install` and no bash-completion framework is detected
- **THEN** the script is written under the encro per-user directory and the bash startup file gains a guarded entry that sources it

### Requirement: Install is idempotent and self-updating

Re-running install SHALL keep exactly one activation entry per shell startup file: existing correct wiring SHALL be left untouched, a missing activation entry SHALL be restored, and duplicates SHALL never be created. When the installed script content is unchanged, the re-run SHALL report that the installation is already current; when the content changed (for example a newer encro version), install SHALL replace the installed script in place.

#### Scenario: Double install is a no-op

- **WHEN** `encro completion powershell --install` is run twice with no change in between
- **THEN** the second run reports the installation is already current
- **AND** the profile contains exactly one encro activation entry

#### Scenario: Changed script is refreshed in place

- **WHEN** install is re-run after the completion script content changed
- **THEN** the installed script file is replaced with the new content
- **AND** each shell startup file still contains exactly one encro activation entry

### Requirement: Uninstall removes everything install created

`encro completion <shell> --uninstall` SHALL remove the activation entry it previously added and the installed script file, leaving all unrelated profile content untouched. A profile or startup file that is empty after the activation entry is removed SHALL be deleted rather than left as an empty file; a file with any other content SHALL keep that content exactly. Uninstalling when nothing is installed SHALL succeed as a no-op and say so.

#### Scenario: Uninstall reverses a previous install

- **WHEN** the user runs `encro completion powershell --install` and then `encro completion powershell --uninstall`
- **THEN** the activation entry is gone from the profiles, the installed script file is deleted, and all other profile lines remain intact

#### Scenario: Uninstall deletes files left empty after unwiring

- **WHEN** uninstall removes the encro block from a profile or startup file and the remaining content is empty (whether install created the file or it was already empty)
- **THEN** the file is deleted rather than left empty

#### Scenario: Uninstall without install is a no-op

- **WHEN** the user runs `encro completion bash --uninstall` without a prior install
- **THEN** the command succeeds and reports that nothing was installed

### Requirement: Install and uninstall are mutually exclusive

Specifying `--install` and `--uninstall` together SHALL be a usage error and SHALL NOT modify any file.

#### Scenario: Both flags together are rejected

- **WHEN** the user runs `encro completion bash --install --uninstall`
- **THEN** the command exits with a usage error and no files are created, modified, or deleted

### Requirement: Profile editing is guarded, minimal, and shell-safe

Activation entries added to shell startup files SHALL be delimited by recognizable encro-specific markers. Editing SHALL preserve all existing file content exactly and append or remove only the delimited block. Entries written for bash SHALL use LF line endings and remain valid to source.

#### Scenario: Markers delimit the encro block

- **WHEN** install wires a PowerShell profile or a bash startup file
- **THEN** the added lines are enclosed in encro-specific begin/end markers that uninstall matches exactly

#### Scenario: Bash startup file remains sourceable

- **WHEN** a bash startup file with a pre-existing content is wired by install
- **THEN** sourcing that startup file in bash completes without syntax errors (no carriage-return artifacts)
