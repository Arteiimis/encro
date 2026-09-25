# cancellation-reporting Specification

## Purpose

Defines what a user sees and what the process exits with when a stop request (Ctrl-C, `CTRL_BREAK`, `SIGINT`) aborts a run: exactly one cancellation notice naming the stage that stopped, the cancellation exit code, and the rule that files killed by the stop are never reported as work that failed.

## Requirements

### Requirement: A stop request aborts the running stage

A stop request SHALL end the run at the running stage's next checkpoint and SHALL NOT let that stage report itself as completed: no item that has not started yet SHALL start, an in-flight child process SHALL be terminated (`subprocess-exec`), and the aborted stage SHALL print no summary line, plan block, report or success line.

#### Scenario: Pictures stop compressing

- **WHEN** a stop request arrives while a picture run compresses a batch
- **THEN** pictures that have not started are not compressed, the run packs nothing, and no compression summary line prints

#### Scenario: Organize stops copying

- **WHEN** a stop request arrives while an organize run copies analyzed images
- **THEN** images that have not started are not copied, the report does not print, and the run does not exit successfully

#### Scenario: Packing stops writing archives

- **WHEN** a stop request arrives while a pack step writes archives
- **THEN** the origins of archives that have not started are not packed and no packing completion line prints

### Requirement: An aborted stage prints one cancellation notice

The stage that a stop request aborts SHALL print exactly one cancellation notice on stderr (`console-output-conventions`) before the run ends. The notice SHALL name that stage and read `<Stage> canceled by user.` — one of `Probing canceled by user.` for video probing, `Encoding tasks canceled by user.` for the encode batch, `Packing canceled by user.` for packing (with and without saved job state), `Preview canceled by user.` for previewing (whichever step the stop reached), `Organize canceled by user.` for organizing (its analysis phase and its copy phase), `Compression task canceled by user.` for picture compression, and `Video conversion canceled by user.` for the video-to-WebP conversion. The notice SHALL print even when the run is quiet, a stage that never started or had no work to do SHALL print none (a resumed encode run whose videos are already encoded prints no encode notice), and a stage whose prompt the user declined SHALL NOT print a second notice for the same event. A stop that reaches no checkpoint at all — a run the force-exit watchdog ends — is outside this rule: that path's output is best-effort, because ending a hung stage is the watchdog's purpose.

#### Scenario: Probe abort is announced

- **WHEN** a stop request aborts video probing
- **THEN** stderr carries `warning: Probing canceled by user.` and stdout carries no plan block and no probe summary line

#### Scenario: Encode abort is announced

- **WHEN** a stop request aborts the encode batch
- **THEN** stderr carries `warning: Encoding tasks canceled by user.` and stdout carries no encode summary line and no failed-file list

#### Scenario: Packing abort is announced in both modes

- **WHEN** a stop request aborts packing, with and without saved job state
- **THEN** each run prints `warning: Packing canceled by user.` once on stderr, and neither run prints a packing error line

#### Scenario: Preview abort is announced

- **WHEN** a stop request aborts a preview before or during its render
- **THEN** stderr carries `warning: Preview canceled by user.` and stdout carries no window list and no written-to line

#### Scenario: Organize abort is announced

- **WHEN** a stop request aborts organizing, during analysis or during copying
- **THEN** stderr carries `warning: Organize canceled by user.` and stdout carries no organize report

#### Scenario: Quiet runs still report the cancellation

- **WHEN** a run with `--quiet` is aborted by a stop request
- **THEN** the cancellation notice still prints on stderr

#### Scenario: A stage that never ran prints no notice

- **WHEN** a stop request arrives before a stage started, so the run ends without that stage running
- **THEN** no cancellation notice for that stage prints

#### Scenario: A stage with nothing to do prints no notice

- **WHEN** a stop request arrives while an encode stage has no video left to encode (every task already complete from a previous run)
- **THEN** no encode cancellation notice prints

#### Scenario: A declined prompt is reported once

- **WHEN** the user declines the encode prompt or the picture-packing prompt
- **THEN** that prompt's cancellation notice prints once and no stop-request notice prints for that stage

### Requirement: A stop-aborted run exits with the cancellation exit code

A run that a stop request aborted SHALL end with the cancellation exit code, the same value `subprocess-exec` assigns to a terminated child (130), whatever the stage was: probing, encoding, packing, previewing or organizing. A run whose work finished before the stop arrived SHALL keep its normal exit code and SHALL NOT print a cancellation notice.

#### Scenario: Every abort path reports 130

- **WHEN** a stop request aborts probing, encoding, resumable packing, non-resumable packing, previewing or organizing
- **THEN** each of those runs exits 130

#### Scenario: A stop after the last stage keeps the success code

- **WHEN** a stop request arrives after the run's last stage completed successfully
- **THEN** the run exits 0 and prints no cancellation notice

### Requirement: The stop's victims are not reported to the user as failures

A child process terminated by the stop request SHALL NOT be described to the user as a failed item: it SHALL NOT appear in a failed-file list, carry a failure reason on the console, produce a stage error line, or be counted as failed or skipped in the stage's own reporting. The test of that termination is the stop being pending when the child's result is handled, not the child's exit code: a console event reaches the child directly, so a killed child reports its own code (130, 255, `-1073741510`), while a termination the run requested reports the cancellation exit code (`subprocess-exec`). Durable per-task state is outside this rule: a task the stop killed keeps the status the run records today, because resume depends on it, and the log keeps the child's own failure reason.

#### Scenario: Killed compression children are not listed

- **WHEN** a stop request kills in-flight picture compressions
- **THEN** stdout carries no `<path>: exit code <n>` line and the run does not report the killed pictures as failed or skipped

#### Scenario: A killed preview render is not a render failure

- **WHEN** a stop request terminates the preview render child
- **THEN** the run prints the cancellation notice and not a preview-generation failure or its child exit code

#### Scenario: A killed preview probe is not a scoring failure

- **WHEN** a stop request terminates a probe step of a preview
- **THEN** the run prints the cancellation notice and not a probe-skipped or scoring-failure warning

#### Scenario: Resume state keeps the task record the stop produced

- **WHEN** a stop kills an in-flight encode task and the run is resumed
- **THEN** the resumed run re-runs that task and leaves the already-encoded tasks alone, whatever status the kill recorded for it
