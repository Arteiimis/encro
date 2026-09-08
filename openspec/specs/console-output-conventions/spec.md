# console-output-conventions Specification

## Purpose

Defines how every user-facing console message renders and which stream it goes to: plain-text severity prefixes that survive color disabling, a single stdout/stderr discipline, and badge-free prompts, summaries, and narration.

## Requirements

### Requirement: Severity is carried by always-printed text prefixes

Error, warning, and hint messages SHALL begin with the lowercase text prefix `error: `, `warning: `, or `hint: ` followed by the message, in every output mode and across all subcommands (including `config` and `completion`). Color SHALL only decorate the prefix; with colors disabled (`--color never`, `NO_COLOR`, or a non-TTY stream) the prefix SHALL remain. Success, informational, result, and heading messages SHALL carry no severity prefix or bracketed badge — their text alone states the outcome. Bracketed kind badges (`[error]`, `[warn]`, `[done]`, `[info]`, `[hint]`, `[?]`) SHALL NOT appear on the product console channel. The verbose echo stream is exempt: its level tags are defined by `logging-behavior`, not this capability.

#### Scenario: Error prefix survives color disabling

- **WHEN** a run fails with `--color never` (or with `NO_COLOR` set, or with stderr piped)
- **THEN** the failure line on stderr begins with the literal text `error: `

#### Scenario: Colored output does not double-mark the severity

- **WHEN** a run fails with colors enabled
- **THEN** the failure line contains exactly one severity marker: a colored `error:` prefix, with no additional `[error]` badge or capitalized `Error:` literal in the message text

#### Scenario: Success lines are unprefixed

- **WHEN** a packing or encoding run completes successfully
- **THEN** the completion line starts with the message text itself and carries no prefix or badge

#### Scenario: Subcommand errors use the same prefix

- **WHEN** the user runs `encro config get <unknown-key>`
- **THEN** the diagnostic on stderr begins with `error: `

### Requirement: Message kind decides the console stream

Errors, warnings, and hints SHALL be written to stderr, including standalone cancellation notices and in-pipeline diagnostics (scan failures, probe failures, per-file warnings). Narration, progress, results, help text, and run-summary blocks — the summary count line, failed-file lists, "Needs attention" entries, and preview hints as one unit — SHALL be written to stdout, so that redirecting stdout captures the complete product output of a run without swallowing or splitting its diagnostics. This SHALL hold for the top-level failure path and subcommand diagnostics alike.

#### Scenario: Failed run redirects cleanly

- **WHEN** a run fails and the user redirects only stdout (`encro bad.mp4 > out.txt`)
- **THEN** the error line and the `hint: Log file: ...` line appear on stderr, and `out.txt` contains neither

#### Scenario: Successful run's product output is capturable

- **WHEN** a successful pack run is redirected (`encro -z <dir> -o <out> > out.txt`)
- **THEN** `out.txt` contains the scan and completion lines of the run

#### Scenario: In-pipeline warnings do not pollute stdout

- **WHEN** a video run prints a mid-run warning (for example an oversized item skipped for packing)
- **THEN** the warning line appears on stderr

#### Scenario: Failed-file list stays with the summary on stdout

- **WHEN** an encode run ends with failures and prints its summary block
- **THEN** the count line, the failed-file list, and the attention entries appear together on stdout, while the standalone error diagnostics of the run went to stderr

#### Scenario: Cancellation notice goes to stderr

- **WHEN** the user cancels a run and a cancellation notice prints
- **THEN** the notice appears on stderr

### Requirement: Prompts render as plain questions

Interactive confirmation prompts SHALL print the question text ending with the choice marker `(Y/n): ` on stdout, with no badge or severity prefix. The interaction semantics are unchanged: Enter defaults to yes, only `y`/`Y` confirms, and `--yes` skips the prompt entirely.

#### Scenario: Encode confirmation prompt

- **WHEN** an encode run reaches its confirmation prompt
- **THEN** stdout shows the question line ending in `(Y/n): ` with no leading badge
