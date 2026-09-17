## Purpose

Defines the semantic color roles every console surface draws from — messages, styled values, help text, and progress bars — and the invariants that keep styling legible across terminal themes and non-overlapping within a line.

## ADDED Requirements

### Requirement: One semantic role palette

Console styling SHALL be expressed through exactly six semantic roles: `Default`, `Muted`, `Accent`, `Good`, `Warn`, and `Bad`. Every role's foreground SHALL resolve either to the terminal's own default foreground or to one of its 16 palette slots. No output path SHALL select a 24-bit RGB or a 256-color-indexed foreground value, whether it emits an escape sequence or sets a console attribute — progress bars color through the console attribute API on Windows, so a requirement phrased only in terms of escape sequences would be vacuous there. Legibility on a light or dark background therefore depends on the user's terminal theme and never on a value the application chose. The verbose echo stream (`-v`, `-vv`) is exempt: its level tags and their colors come from the logging echo sink's own configuration, not from this capability.

#### Scenario: Only palette-slot foregrounds are selected

- **WHEN** any console output — messages, help, or progress bars — is produced with colors forced on
- **THEN** every foreground it selects is either the terminal's default foreground or one of its 16 palette slots, and no 24-bit RGB or 256-color-indexed value appears on either the escape-sequence path or the console-attribute path

#### Scenario: Every product-channel surface draws from the same role set

- **WHEN** a message, a styled value, help text, and a progress bar each render with colors forced on
- **THEN** the foreground sequences they emit are all drawn from the six roles' slots, with no surface supplying a color of its own

### Requirement: Role assignment

Each role SHALL have one assigned meaning and one fixed rendering:

- `Muted` — the terminal's default foreground with faint emphasis: hint diagnostics. This role is text-only; no progress bar uses it.
- `Accent` — the terminal's cyan slot: values embedded in message text (paths, counts), option and subcommand names, their `(=default)` suffixes, and idle, active and overall progress bars.
- `Good` — the terminal's green slot: the leading verb of success and run-result lines, and completed progress bars.
- `Warn` — the terminal's yellow slot: warning diagnostics, and packing and finalizing progress bars.
- `Bad` — the terminal's red slot: error diagnostics and failed progress bars.
- `Default` — no styling: message prose, plain product output, help descriptions, usage lines, version text, and anything not assigned above. Help section headings are not in this list; they are styled by weight (below).

`Muted` deliberately spends no palette slot. Themes exist whose bright-black slot is their background (Solarized Dark maps it to base02), which would render hints invisible; faint dims whichever foreground the user chose and degrades to "not dimmed" at worst.

Progress bars use `Accent` whether they are idle or active. A bar cannot carry `Muted`: the bar library colors a frame by setting a foreground before each bar and resetting once after the whole frame, so a bar left with no color of its own inherits the preceding bar's — inside a frame that also holds an `Accent` bar, "no color" renders accent, not the default foreground. The library cannot express a per-bar default inside a multi-bar frame, and the idle slot bars already name themselves (`Encoding: [idle-3]`) at zero progress, so a color of their own would be redundant rather than informative.

#### Scenario: Hint diagnostics are muted

- **WHEN** a hint diagnostic prints with colors enabled
- **THEN** its `hint:` prefix renders in the terminal's default foreground with faint emphasis and selects no palette slot

#### Scenario: Paths and counts share one accent

- **WHEN** a message body names a path and a count with colors enabled
- **THEN** both use the terminal's cyan slot and the prose around them is unstyled

#### Scenario: Idle bars share the active color

- **WHEN** idle, active, packing, completed, and failed progress bars render together in one frame with colors enabled
- **THEN** the idle bar and the active bar both use the cyan slot, and the packing, completed and failed bars use the yellow, green and red slots respectively, with no bar left to inherit a neighbour's color

#### Scenario: Unstyled elements carry no styling

- **WHEN** help descriptions, usage lines, and version text print with colors enabled
- **THEN** they contain no styling escape sequence

#### Scenario: Option defaults are accented and faint

- **WHEN** an option line rendering an `(=default)` suffix prints with colors enabled
- **THEN** the suffix carries the accent role with faint emphasis

### Requirement: Styling lands on tokens, never on nested spans

Styled spans within one printed line SHALL be disjoint: no styled span SHALL contain another. A message that maps to a role SHALL style either its severity prefix or its leading verb — never its whole body — and every value embedded in that message SHALL carry its own role-styled span with its own boundaries. Characters outside a styled span SHALL render in the terminal's default foreground. When a message's text already begins with a role-styled value supplied by its caller, the value's span SHALL stand and no leading-verb styling SHALL be applied to it; the disjointness rule takes precedence over styling a verb.

#### Scenario: Prose around an embedded value keeps one appearance

- **WHEN** a status line naming a count and then a path prints with colors enabled
- **THEN** the count and the path are accent-styled, and every other character of the line renders in the terminal's default foreground

#### Scenario: A leading verb is styled and its body is not

- **WHEN** a run's result line prints with colors enabled
- **THEN** only the line's first word carries the good role, and the rest of the line is unstyled apart from accent-styled values

#### Scenario: A caller-styled first value wins over verb styling

- **WHEN** a message whose first format argument is a role-styled value prints through a leading-verb kind with colors enabled
- **THEN** that value keeps its own span and no additional span is opened around it, so the line still contains no nested span

#### Scenario: No span is nested inside another

- **WHEN** any console line prints with colors enabled
- **THEN** its escape sequences form a flat sequence of spans that never contain one another

### Requirement: Help structure is carried by weight, not color

Help section headings SHALL render with bold emphasis and SHALL NOT consume a color role, so that help structure does not compete with option and subcommand names for the accent slot. Option names, subcommand names, and `(=default)` suffixes SHALL use the accent role; option and subcommand descriptions SHALL carry no styling.

#### Scenario: Help headings are bold and uncolored

- **WHEN** the user runs `encro -h` with colors enabled
- **THEN** each section heading renders with bold emphasis and no color escape sequence

#### Scenario: Option names are accented

- **WHEN** an option line with a default value renders with colors enabled
- **THEN** the option name and its `(=default)` suffix carry the accent role and the description is unstyled

### Requirement: Disabled styling emits nothing and substitutes nothing

When styling is disabled — by `--color never`, a `NO_COLOR` value, a non-TTY stream, or `TERM=dumb` — no styling escape sequence SHALL be emitted, and no role SHALL be replaced by a fixed color. Every element SHALL fall back to the terminal's default foreground so that legibility never depends on which background the user has.

#### Scenario: Disabled colors emit no styling sequences

- **WHEN** any console output, help text, or progress bar renders with colors disabled
- **THEN** it contains no styling escape sequence

#### Scenario: Every bar in a disabled frame resolves to no color

- **WHEN** a frame of progress bars renders with colors disabled
- **THEN** every bar's role resolves to no color rather than to a fixed color such as white, so that no bar sets a foreground and the whole frame stays in the terminal's default foreground

#### Scenario: Bold is suppressed with styling

- **WHEN** help text renders with colors disabled
- **THEN** its headings contain no bold escape sequence
