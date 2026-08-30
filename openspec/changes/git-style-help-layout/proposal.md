# Proposal: git-style-help-layout

## Why

The help output pads every description to a fixed minimum column (34), so short option names such as `--help,-h` are followed by up to 25 spaces before their description. Meanwhile the only true subcommands (`preview`, `config`) are discoverable only as bare synopsis lines in the usage block, with no description of what they do. `git -h` demonstrates the reference layout: a description column that hugs the widest rendered command name, plus a command list that names and describes each subcommand.

## What Changes

- Auto-fit the help description column: the column starts at `2 (indent) + widest first-column text + 3 (gap)`, computed per rendered help. The brief tier (`-h`), full tier (`-hh`), and each subcommand help (`preview -h`, `config -h`) compute the width from the options actually rendered in that help; the width is global across option groups, not per group. The existing narrow-terminal cap (`COLUMNS`-derived maximum description column) is kept so line-length guarantees still hold.
- Add an `encro commands:` section between the usage block and the option groups in the main help, listing `preview` and `config` with their one-line descriptions (reused from the registered subcommand descriptions — no new strings).
- Slim the main-help usage section: the `encro preview <original> [...]` and `encro config <list|...>` synopsis lines are removed because the commands section now carries them. The encode, picture, zip, and `encro -h | -hh | --version` lines stay. Subcommand help keeps its own usage line.
- Fix a layout quirk: when `COLUMNS` is set, the first description line wraps at a width derived from the ANSI-escaped rendered text (larger than its display width), so with color enabled the first line wraps earlier than continuation lines. Wrap widths must be computed from plain-text widths only, making help layout identical across color modes.

## Capabilities

### New Capabilities

- `cli-help-layout`: the description-column auto-fit rule (per-tier, global across groups, gap 3, narrow-terminal cap), the `encro commands:` section, the slimmed main-help usage section, and color-mode-invariant layout.

### Modified Capabilities

<!-- None. cli-help-tiering's requirements (grouping, tier contents, hint line, tier-advertising usage line) remain true under the new layout; the byte-stability requirement in cli11-native-validation is satisfied because this change declares the new layout in a capability spec. -->

## Impact

- Code: `src/cmd/cmd.cpp` only — the two help formatters (main and subcommand) share the column-width computation; the duplicated clamp block is extracted into one helper. No CLI11 registration or parsing changes.
- Tests: existing help assertions (option presence/absence, hint line, `COLUMNS` line-length caps, colored-default alignment, `findHelpLine` probes) remain valid; new tests pin the column rule, the commands section, the slimmed usage, and color-mode invariance.
- Specs: no existing requirement changes; the new capability declares the layout deltas required by the "Main help output stays byte-stable" requirement in `cli11-native-validation`.
