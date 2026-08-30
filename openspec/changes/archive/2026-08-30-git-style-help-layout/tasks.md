# Tasks: git-style-help-layout

## 1. Tests first (TDD — write before implementation, watch them fail)

- [x] 1.1 Column auto-fit tests in `tests/cmd_cmd_tests.cpp`: brief tier (`-h`) — the widest first-column line is followed by exactly 3 spaces before its description and every other option description starts at that same column; full tier (`-hh`) — same rule including advanced lines, across all four groups.
- [x] 1.2 Subcommand column tests: `preview -h` and `config -h` descriptions align after the widest first-column text of the subcommand's own options with the 3-space gap.
- [x] 1.3 Commands-section and usage tests: `-h` and `-hh` contain an `encro commands:` section listing `preview` and `config` with their registered descriptions; main usage has no `encro preview` / `encro config` line but keeps `encro -h | -hh | --version` and the positional-input form; `preview -h` keeps its own usage line.
- [x] 1.4 Color-invariance test: with `COLUMNS=120`, `-hh` rendered with color forced on and with color disabled is line-identical after `stripAnsi`.

## 2. Implementation (`src/cmd/cmd.cpp`)

- [x] 2.1 Extract a shared `computeColumnWidth` helper (`min(widest + 3, layout cap)`) and replace the duplicated 34..48 clamp block in both `makeHelpFormatter` and `makeSubcommandHelpFormatter`.
- [x] 2.2 Drop the `renderedDescriptionColumn` / `explicitWidthConstraint` plumbing and always wrap descriptions from the plain-text `displayDescriptionColumn` (color-mode invariance).
- [x] 2.3 Remove the `encro preview` / `encro config` lines from the main usage array; render the `encro commands:` section after the usage block from the registered subcommand apps (name + description via the app pointers, existing message kinds).

## 3. Verification

- [x] 3.1 `xmake test-parallel` passes (unit + e2e shards).
- [x] 3.2 Visual check of `encro -h`, `encro -hh`, `encro preview -h`, `encro config -h` against the spec examples (columns 31/36, commands section, slimmed usage) and `COLUMNS=72` line-cap check.
- [x] 3.3 `xmake fmt -k` and `xmake tidy` clean on touched files.
