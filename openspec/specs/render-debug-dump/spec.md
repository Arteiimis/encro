# render-debug-dump Specification

## Purpose

Gives non-interactive runs (pipes, tests, agent sessions) inspectable console output: an opt-in environment-activated dump that records exactly what the console layer rendered — both streams in write order, with styling bytes, plus each progress bar's final state — so layout, styling and cross-stream interleaving can be checked without owning a TTY.

## Requirements

### Requirement: Render dump is activated by an environment variable

The console layer SHALL append every message-layer console write (every write that goes through the console output functions) to the file named by a non-empty `ENCRO_DEBUG_DUMP_RENDER` environment variable, creating or appending to that file per write. When the variable is unset or empty, the console layer SHALL behave exactly as it does without this capability: no file is created and console output is byte-identical to a run of a build without this capability. Activation SHALL NOT change what is rendered — dump records are copies of the rendered bytes, never a re-rendering or a reformatting of them, and quiet-mode suppression applies before the dump just as it applies before the console. Progress bars' own terminal writes are not message-layer writes and are covered by the final-state frame requirement instead.

#### Scenario: Unset variable leaves no trace

- **WHEN** the application runs with `ENCRO_DEBUG_DUMP_RENDER` unset or empty and produces console output
- **THEN** no dump file is created and console output is byte-identical to a run of a build without this capability

#### Scenario: Set variable captures console writes

- **WHEN** the application runs with `ENCRO_DEBUG_DUMP_RENDER=<file>` set to a writable path and prints messages to stdout and stderr
- **THEN** the named file exists after the run and contains a record of every console write

### Requirement: Dump records carry the stream marker and the rendered bytes

Each dump record SHALL be the target stream's marker (`[out]` for stdout, `[err]` for stderr) followed by the exact bytes written to that stream, including any styling escape sequences that were part of the rendered write. Records from both streams SHALL append to one file in write order, so the relative order of stdout and stderr writes is observable from the dump alone. The dump SHALL NOT strip, rewrite or add styling: a colors-forced run dumps styled bytes, and the same run with colors off dumps the identical text without sequences.

#### Scenario: Cross-stream order is preserved

- **WHEN** a run writes a stdout message, then a stderr warning, then another stdout message with the dump active
- **THEN** the dump shows the three records in that order, each prefixed with its stream's marker

#### Scenario: Styling bytes appear only when colors were enabled

- **WHEN** the same message is dumped once from a run with colors forced on and once from a run with colors off
- **THEN** the first dump record contains the message text wrapped in its styling escape sequences and the second contains the identical text with no escape sequences

### Requirement: Progress bars leave a final-state frame in the dump

When a progress-bar phase ends and its bars are cleared, each bar the phase created SHALL contribute one plain-text line to the dump describing that bar's final state — the progress fill and percentage for a determinate bar, or the indeterminate marker for a bar left in its indeterminate state at the end of the phase, followed by the bar's stored label text (the label the phase last set on the bar, seeded from the prompt at creation). The frame SHALL be appended for non-TTY runs, where no bar was ever drawn on the console, and for TTY runs alike, because the frame is written to the dump file and not to the console. Console output SHALL NOT change: no cursor movement, bar text or extra line is written to any console stream by the frame append.

#### Scenario: Non-TTY run records the bars it never drew

- **WHEN** a run with stdout piped (bars suppressed on the console) and the dump active finishes a phase that created two bars
- **THEN** the dump contains two frame lines carrying those bars' final states, and the piped stdout carries no bar text or cursor control

#### Scenario: TTY run records the same frames without touching the screen

- **WHEN** a run on a terminal with the dump active finishes a bar phase
- **THEN** the dump contains the same final-state frames and the terminal shows exactly what a run without the dump active would show
