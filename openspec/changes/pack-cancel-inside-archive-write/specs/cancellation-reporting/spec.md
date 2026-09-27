## MODIFIED Requirements

### Requirement: A stop request aborts the running stage

A stop request SHALL end the run at the running stage's next checkpoint and SHALL NOT let that stage report itself as completed: no item that has not started yet SHALL start, an in-flight child process SHALL be terminated (`subprocess-exec`), and the aborted stage SHALL print no summary line, plan block, report or success line. An archive's entry write SHALL be a checkpoint: a stop that arrives while entries are being written SHALL end that archive at its next entry, and the abandoned archive SHALL NOT be left behind as a half-written file that could pass for a completed pack.

#### Scenario: Pictures stop compressing

- **WHEN** a stop request arrives while a picture run compresses a batch
- **THEN** pictures that have not started are not compressed, the run packs nothing, and no compression summary line prints

#### Scenario: Organize stops copying

- **WHEN** a stop request arrives while an organize run copies analyzed images
- **THEN** images that have not started are not copied, the report does not print, and the run does not exit successfully

#### Scenario: Packing stops writing archives

- **WHEN** a stop request arrives while a pack step writes archives
- **THEN** the origins of archives that have not started are not packed, an archive whose entries are already being written stops at its next entry and leaves no half-written archive behind, and no packing completion line prints
