# cli11-native-validation Specification

## MODIFIED Requirements

### Requirement: Main help output stays byte-stable

The two-tier help contract (`-h` brief tier, `-hh` full tier, option groups, advanced-option hiding, hint line, `(=default)` displays) SHALL remain stable across changes: help output SHALL change only in ways that an explicit capability specification declares (such as usage lines advertising subcommands, collapsed `--[no-]name` option-name rendering, or config-driven effective default displays), and SHALL otherwise preserve the same structure.

#### Scenario: Brief and full help unchanged

- **WHEN** the user runs `encro -h` and `encro -hh` and no capability specification declares a help-output change affecting the rendered content
- **THEN** the output preserves the same structure: same usage lines, same groups, same advanced-option hiding, same `Run 'encro -hh' to view all options.` hint, and same `(=value)` default displays
