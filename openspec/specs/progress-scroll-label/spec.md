# progress-scroll-label Specification

## Purpose

Provides a scrolling (marquee-style) display for progress-bar postfix text that overflows the terminal's postfix width, so long encode filenames and status text remain readable on narrow terminals instead of being cut off with an ellipsis.

## Requirements

### Requirement: Overflowing postfix text scrolls instead of truncating

Progress-bar postfix text (e.g., `Encoding: <filename> | <status>`) whose display width exceeds the available postfix budget SHALL be shown as a continuously scrolling window of the full text instead of being truncated with an ellipsis. Text that fits within the budget SHALL be shown statically in full. The scrolling window SHALL be no wider than the postfix budget, SHALL advance monotonically over time, SHALL wrap around to the beginning of the text, and SHALL never cut a UTF-8 code point in half.

#### Scenario: Long filename on narrow terminal scrolls

- **WHEN** the encoded filename makes the postfix text wider than the postfix budget
- **THEN** a window of the full text no wider than the budget is shown
- **AND** the window advances over time to the end of the text, pauses briefly, then reverses direction and scrolls back, alternating continuously
- **AND** all text, including the full filename, is eventually visible while the task runs
- **AND** the window never shows a blank region: there is no scroll phase where the window contains only padding instead of text

#### Scenario: Text fits within budget stays static

- **WHEN** the postfix text is no wider than the postfix budget
- **THEN** the full text is shown without scrolling or ellipsis truncation

#### Scenario: Ellipsis truncation no longer used for postfix overflow

- **WHEN** postfix text overflows the budget
- **THEN** no ellipsis is appended and the scroll window replaces the old truncated-with-ellipsis display

#### Scenario: UTF-8 names scroll without corruption

- **WHEN** the postfix text contains multi-byte UTF-8 characters (e.g., CJK filenames)
- **THEN** every rendered frame of the scroll window contains only whole code points

#### Scenario: Only the label part scrolls, status stays fixed

- **WHEN** the postfix text has multiple parts separated by ` | ` (e.g., `Encoding: <filename> | segment 1/1`)
- **THEN** only the first part (the label, e.g., `Encoding: <filename>`) scrolls within its own window
- **AND** the remaining parts (e.g., the segment status) and the ` | ` delimiters stay in a fixed position and never scroll off-screen
- **AND** the full display width never exceeds the postfix budget

#### Scenario: Oversized status is truncated, not scrolled

- **WHEN** the fixed status part alone is too wide to leave a minimal scrolling window for the label
- **THEN** the status part is truncated with an ellipsis and the label keeps a minimal scrolling window

### Requirement: ETA prefix stays visible while content scrolls

When a progress bar shows an ETA prefix, the ETA prefix SHALL remain visible (not scrolled away) while the rest of the postfix text scrolls. The scroll window applies only to the postfix content, never to the ETA prefix.

#### Scenario: ETA shown while encode label scrolls

- **WHEN** a bar has an ETA prefix and the postfix text overflows the budget
- **THEN** the ETA prefix is always visible on every redraw
- **AND** only the postfix content (e.g., `Encoding: <filename> | <status>`) scrolls within the remaining width

### Requirement: Encode label and status reach the scroller untruncated

The filename label and status text passed to the progress postfix SHALL NOT be pre-truncated at the current fixed limits (48-column filename label, 72-column status line) before reaching the scroll display. A sanity cap on raw ffmpeg status lines SHALL remain so a single pathological line cannot consume unbounded memory; the cap SHALL be large enough that realistic filenames and status text are unaffected.

#### Scenario: Filename longer than 48 columns scrolls fully

- **WHEN** an encoded file's name is longer than 48 display columns
- **THEN** the full name appears in the postfix text and is reachable via the scroll window

#### Scenario: Status line longer than 72 columns scrolls fully

- **WHEN** a reported encode status line is longer than 72 characters but under the sanity cap
- **THEN** the full status text appears in the postfix text and is reachable via the scroll window
