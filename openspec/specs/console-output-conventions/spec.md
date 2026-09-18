# console-output-conventions Specification

## Purpose

Defines how every user-facing console message renders and which stream it goes to: plain-text severity prefixes that survive color disabling, a single stdout/stderr discipline, and badge-free prompts, summaries, and narration.

## Requirements

### Requirement: Severity is carried by always-printed text prefixes

Error, warning, and hint messages SHALL begin with the lowercase text prefix `error: `, `warning: `, or `hint: ` followed by the message, in every output mode and across all subcommands (including `config` and `completion`). The prefix SHALL carry its message kind's color role as defined by `terminal-color-palette`; with colors disabled (`--color never`, `NO_COLOR`, or a non-TTY stream) the uncolored prefix text SHALL remain. Colors SHALL extend past the prefix only through values embedded in the message body — a path, a count, or another named value, each carrying its own style boundary — and never through a style applied to the message body as a whole. Success, informational, result, and help-heading text SHALL carry no severity prefix or bracketed badge — their text alone states the outcome, and any emphasis they carry SHALL be a leading-verb color role or bold weight rather than a body-wide color. Bracketed kind badges (`[error]`, `[warn]`, `[done]`, `[info]`, `[hint]`, `[?]`) SHALL NOT appear on the product console channel. The verbose echo stream is exempt: its level tags and their colors come from the logging echo sink's own configuration, not from this capability.

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

#### Scenario: Embedded values carry their own style boundary

- **WHEN** a warning message whose body names a path and a byte count prints with colors enabled
- **THEN** the `warning:` prefix, the path, and the count each carry a color role, and the prose between them renders in the terminal's default foreground

### Requirement: Message kind decides the console stream

Errors, warnings, and hints SHALL be written to stderr, including standalone cancellation notices and in-pipeline diagnostics (scan failures, probe failures, per-file warnings). Narration, progress, results, help text, and run-summary blocks — the summary count line, failed-file lists, the attention block including its severity-marked announcement, and preview hints as one unit — SHALL be written to stdout, so that redirecting stdout captures the complete product output of a run without swallowing or splitting its diagnostics. A severity-marked line belonging to the run-summary block SHALL stay on stdout even though a standalone diagnostic of the same severity goes to stderr; the block travels as one unit. This SHALL hold for the top-level failure path and subcommand diagnostics alike.

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

#### Scenario: Summary block stays together despite a severity marker

- **WHEN** an encode run ends with an attention block whose announcement line begins with `warning: `
- **THEN** that announcement and its items appear on stdout, not on stderr

#### Scenario: Cancellation notice goes to stderr

- **WHEN** the user cancels a run and a cancellation notice prints
- **THEN** the notice appears on stderr

### Requirement: Prompts render as plain questions

Interactive confirmation prompts SHALL print the question text ending with the choice marker `(Y/n): ` on stdout, with no badge or severity prefix. The interaction semantics are unchanged: Enter defaults to yes, only `y`/`Y` confirms, and `--yes` skips the prompt entirely.

#### Scenario: Encode confirmation prompt

- **WHEN** an encode run reaches its confirmation prompt
- **THEN** stdout shows the question line ending in `(Y/n): ` with no leading badge

### Requirement: A diagnostic group carries one severity marker

When a run rolls several warnings into one reported group, the group SHALL print one severity-marked announcement line naming the item count, and each item SHALL print as an indented line formatted `<subject>: <reason>` in the terminal's default foreground — items carry no color role of their own, unlike the failed-file list's paths. Item lines SHALL NOT repeat the severity prefix. The announcement SHALL read as a sentence naming the count rather than as a label ending in a colon followed by its content. The group SHALL remain part of the run-summary block written to stdout.

#### Scenario: Attention block prints one marker

- **WHEN** a run ends with warnings rolled into a group of two items
- **THEN** exactly one line of the block begins with `warning: ` and that line names the count
- **AND** both item lines are indented and carry no `warning: ` prefix

#### Scenario: Announcement is not a label

- **WHEN** the group announcement prints
- **THEN** it is a sentence naming the item count and does not end in a colon

#### Scenario: Items name subject then reason

- **WHEN** an item of the group prints
- **THEN** the line reads `<subject>: <reason>` with the subject first and carries no color role

#### Scenario: Group stays with the summary on stdout

- **WHEN** a run that produced an attention group has its stdout redirected to a file
- **THEN** the announcement and its items appear together in that file
