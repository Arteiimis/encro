## ADDED Requirements

### Requirement: Subcommand names take precedence over positional input interpretation

When the first argument matches a known subcommand name, it SHALL select that subcommand and SHALL NOT be treated as a positional input path. Bare invocations without a subcommand SHALL continue to fall through to the existing encode workflow, preserving all current positional input semantics.

#### Scenario: Subcommand wins over positional interpretation
- **WHEN** the user runs `encro preview <a> <b>`
- **THEN** the preview subcommand runs with its own positional arguments, and the word `preview` is not treated as an input path

#### Scenario: Bare invocation falls through to encode
- **WHEN** the user runs `encro <dir>` or `encro <file>` without a subcommand
- **THEN** the existing encode workflow runs with unchanged positional input semantics
