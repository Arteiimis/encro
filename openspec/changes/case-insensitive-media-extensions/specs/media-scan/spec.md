## ADDED Requirements

### Requirement: Media extension matching is case-insensitive

Media extension matching SHALL compare the extension of a candidate file with the configured extension set using an ASCII case fold (A–Z matches a–z), so an uppercase or mixed-case spelling of a listed extension matches. The comparison SHALL NOT depend on the process locale, and it SHALL remain exact on every other character: a file whose extension is not listed, or differs from a listed extension by anything other than letter case, is not a match. The rule SHALL apply to every scan that matches media extensions, whether the scan root is a directory or a single file.

#### Scenario: Uppercase extension matches a lowercase list

- **WHEN** a scan whose extension set holds `.mp4` meets a file named `CLIP.MP4`
- **THEN** the file is reported as a match

#### Scenario: Mixed-case extension matches a lowercase list

- **WHEN** a scan whose extension set holds `.jpg` meets a file named `PHOTO.Jpg`
- **THEN** the file is reported as a match

#### Scenario: Single-file input follows the same rule

- **WHEN** a scan is given a single file whose extension is an uppercase spelling of a listed extension
- **THEN** that file is reported as a match

#### Scenario: A different extension is still excluded

- **WHEN** a scan whose extension set holds `.mp4` meets a file named `notes.txt`
- **THEN** the file is not reported as a match
