## MODIFIED Requirements

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
