## ADDED Requirements

### Requirement: Completion help teaches the flag-first canonical form

The completion subcommand help SHALL present its usage synopsis with the action flags before the shell argument (`encro completion [--install | --uninstall] <powershell|bash>`), followed by a concrete example line showing a complete invocation (for example `encro completion powershell --install`). The synopsis order documents the canonical form; the parser continues to accept the shell argument and the flags in either order.

#### Scenario: Synopsis shows flags before the shell argument

- **WHEN** the user runs `encro completion --help`
- **THEN** the usage section shows `encro completion [--install | --uninstall] <powershell|bash>` as the synopsis
- **AND** a concrete example line naming a full invocation (shell and flag) is shown

#### Scenario: Both argument orders remain valid

- **WHEN** the user runs `encro completion powershell --install` or `encro completion --install powershell`
- **THEN** both invocations are accepted and perform the same install
