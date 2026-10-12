# Spec Delta

## ADDED Requirements

### Requirement: Confirmation is requested once per batch

A batch run SHALL ask a workflow confirmation prompt at most once: the first subfolder that reaches a confirmation asks the user, and every later subfolder SHALL reuse that answer without prompting — including a later subfolder whose confirmation has a different question text. For a batch run, this rule governs the single-directory workflows' confirmation requirements (the encode plan's prompt and the resumed-encode packing question); single-directory runs keep them unchanged.

#### Scenario: One prompt covers every subfolder

- **WHEN** a batch processes three subfolders that each reach the confirmation prompt and the user accepts
- **THEN** the prompt is shown once, before the first subfolder's work, and the later subfolders proceed without prompting

#### Scenario: A declined confirmation stops the batch

- **WHEN** the user declines the confirmation shown for a subfolder while later subfolders remain
- **THEN** that subfolder is marked skipped, every remaining subfolder is marked skipped with a notice naming it, the workflow's decline notice prints once, and the batch exits 0

#### Scenario: A stop during the prompt is not a decline

- **WHEN** a stop request arrives while a subfolder's confirmation is waiting for input
- **THEN** that subfolder follows the existing cancellation path and the remaining subfolders are skipped as a stop, not as a declined confirmation

#### Scenario: The stored answer covers a later, different question

- **WHEN** the first prompting subfolder asks the packing question and a later subfolder would ask the encode question
- **THEN** the later subfolder proceeds without prompting

#### Scenario: --yes skips the prompt entirely

- **WHEN** the batch runs with `--yes`
- **THEN** no confirmation is shown and every subfolder proceeds
