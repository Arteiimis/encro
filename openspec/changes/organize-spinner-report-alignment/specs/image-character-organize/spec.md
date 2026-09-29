## MODIFIED Requirements

### Requirement: Progress and report

During analysis the command SHALL show a progress bar with completion count, image rate, and ETA on a TTY; no progress bar SHALL be rendered when stdout is not a terminal. While the tagger and identity engines load, the command SHALL show an indeterminate spinner on a TTY and print no provider notice until the engines are built, so exactly one provider notice follows the spinner. After execution the command SHALL print a report: per-folder counts, each folder's assignment source (character tag, folder match, new cluster, `mixed/`, `uncategorized/`), and run totals. The report's folder table SHALL keep its columns aligned: the folder-column width SHALL be derived from the folder names present and the terminal width — never below a fixed minimum, never beyond the terminal budget — and every column SHALL be padded to a consistent display width. A folder name wider than the derived column SHALL be truncated with an ellipsis rather than pushing the count and source columns out of alignment.

#### Scenario: Analysis shows progress with rate and ETA

- **WHEN** analysis runs on a TTY with more than a few images
- **THEN** a progress bar shows images completed, images per second, and an ETA

#### Scenario: Engine loading shows a spinner

- **WHEN** the command starts with real (non-fake) engines on a TTY and the model files are being loaded
- **THEN** an indeterminate spinner is shown while the engines load
- **AND** exactly one provider notice is printed after the spinner is cleared

#### Scenario: Report lists folders with sources

- **WHEN** a run completes with folders from character tags, folder matches, and new clusters
- **THEN** the report lists each folder with its image count and assignment source

#### Scenario: Long folder names stay aligned

- **WHEN** a run completes with a folder name wider than the table's folder column
- **THEN** that folder name is truncated with a trailing ellipsis in the report
- **AND** the images and source columns of every row, including that row, align with the header

#### Scenario: Narrow tables stay compact

- **WHEN** a run completes where every folder name fits the table's minimum folder-column width
- **THEN** the folder column stays at that minimum width instead of growing toward the terminal width
