## MODIFIED Requirements

### Requirement: Role assignment

Each role SHALL have one assigned meaning and one fixed rendering:

- `Muted` — the terminal's default foreground with faint emphasis: hint diagnostics. This role is text-only; no progress bar uses it.
- `Accent` — the terminal's cyan slot: values embedded in message text (paths, neutral counts, elapsed durations), option and subcommand names, their `(=default)` suffixes, and idle, active and overall progress bars.
- `Good` — the terminal's green slot: the leading verb of success and run-result lines, the succeeded count in a phase summary line, and completed progress bars.
- `Warn` — the terminal's yellow slot: warning diagnostics, packing and finalizing progress bars, and the skipped/not-probed segment of a phase summary line.
- `Bad` — the terminal's red slot: error diagnostics, failed progress bars, and the failed segment of a phase summary line.
- `Default` — no styling: message prose, plain product output, help descriptions, usage lines, version text, and anything not assigned above. Help section headings are not in this list; they are styled by weight (below).

`Muted` deliberately spends no palette slot. Themes exist whose bright-black slot is their background (Solarized Dark maps it to base02), which would render hints invisible; faint dims whichever foreground the user chose and degrades to "not dimmed" at worst.

A phase summary line's outcome tokens carry the role of the outcome they report, which overrides the general rule that counts are accented: the succeeded count is `Good`, a `(n failed)` segment is `Bad`, and a `(n skipped)` or `(n not probed)` segment is `Warn`. The line's leading verb is `Good` when the phase had neither failures nor skips, `Bad` when it had at least one failure, and `Warn` when it only skipped work. Neutral values keep `Accent`: the total in `<succeeded>/<total>` and the line's elapsed duration. The plan block is unaffected — its header row, file rows and totals line keep the unstyled rendering they have today.

Progress bars use `Accent` whether they are idle or active. A bar cannot carry `Muted`: the bar library colors a frame by setting a foreground before each bar and resetting once after the whole frame, so a bar left with no color of its own inherits the preceding bar's — inside a frame that also holds an `Accent` bar, "no color" renders accent, not the default foreground. The library cannot express a per-bar default inside a multi-bar frame, and the idle slot bars already name themselves (`Encoding: [idle-3]`) at zero progress, so a color of their own would be redundant rather than informative.

#### Scenario: Hint diagnostics are muted

- **WHEN** a hint diagnostic prints with colors enabled
- **THEN** its `hint:` prefix renders in the terminal's default foreground with faint emphasis and selects no palette slot

#### Scenario: Paths and counts share one accent

- **WHEN** a message body names a path and a count that reports no outcome, such as the total in `<succeeded>/<total>`
- **THEN** both use the terminal's cyan slot and the prose around them is unstyled

#### Scenario: Outcome tokens carry their outcome's role

- **WHEN** an encode phase ends with failures and skipped files, and colors are enabled
- **THEN** the succeeded count renders green, the `(n failed)` segment red, and the `(n skipped)` segment yellow, each as its own span, with the surrounding prose unstyled
- **AND** the destination path and the elapsed duration render in the accent role

#### Scenario: Phase verb follows the phase outcome

- **WHEN** a phase ends with at least one failure, or ends having only skipped work
- **THEN** its leading verb renders red in the first case and yellow in the second

#### Scenario: Idle bars share the active color

- **WHEN** idle, active, packing, completed, and failed progress bars render together in one frame with colors enabled
- **THEN** the idle bar and the active bar both use the cyan slot, and the packing, completed and failed bars use the yellow, green and red slots respectively, with no bar left to inherit a neighbour's color

#### Scenario: Unstyled elements carry no styling

- **WHEN** help descriptions, usage lines, and version text print with colors enabled
- **THEN** they contain no styling escape sequence

#### Scenario: Option defaults are accented and faint

- **WHEN** an option line rendering an `(=default)` suffix prints with colors enabled
- **THEN** the suffix carries the accent role with faint emphasis
