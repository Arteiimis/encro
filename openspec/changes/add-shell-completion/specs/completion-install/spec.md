# completion-install Specification

## Purpose

Turns the generated completion scripts into one-command setup: `encro completion <shell> --install` writes the script to a stable per-user location and wires it into the shell's startup, `--uninstall` reverses it completely, and both operations are idempotent and minimally invasive to the user's shell profiles.

## ADDED Requirements

### Requirement: Install activates completion for new shell sessions

`encro completion <shell> --install` SHALL write the completion script for the requested shell to a stable per-user location and register it so that new interactive sessions of that shell load the completion. For PowerShell this SHALL be done by referencing the script from the user's PowerShell profile(s); for bash this SHALL be done via the bash-completion lazy-load location when the bash-completion framework is present, and via the user's bash startup file otherwise. On success the command SHALL print the installed script path and where it was wired.

#### Scenario: PowerShell install wires a profile

- **WHEN** the user runs `encro completion powershell --install`
- **THEN** the script is written under the encro per-user directory and the command reports both that path and the profile file(s) it wired
- **AND** a new PowerShell session dot-sources the script through an entry in the profile

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

`encro completion <shell> --uninstall` SHALL remove the activation entry it previously added and the installed script file, leaving all unrelated profile content untouched. Uninstalling when nothing is installed SHALL succeed as a no-op and say so.

#### Scenario: Uninstall reverses a previous install

- **WHEN** the user runs `encro completion powershell --install` and then `encro completion powershell --uninstall`
- **THEN** the activation entry is gone from the profile, the installed script file is deleted, and all other profile lines remain intact

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
