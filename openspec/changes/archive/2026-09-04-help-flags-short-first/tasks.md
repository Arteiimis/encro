## 1. Tests (TDD: failing first)

- [x] 1.1 Update name-format assertions in `tests/cmd_cmd_tests.cpp` (`--help,-h` → `-h, --help`, `--[no-]recursive,-r` → `-r, --[no-]recursive`, etc.) and recompute the widest-column assertion from `--output-format,-f (=mp4)` to `-f, --output-format (=mp4)`; verify they fail against current code with `xmake test-report --tag="[cmd]"`

## 2. Implementation

- [x] 2.1 In `src/cmd/cmd.cpp`, split `formatOptionName` into short-cell (`-x, ` or 4 blanks) and long-cell (existing long-name loop with `no-` collapse; positionals keep `get_name(true)`) helpers; verify unit tests compile
- [x] 2.2 Update `formatOptionHelp` to render short cell + long cell + description, and `computeMaxColumnLen` to measure the long cell only; verify `xmake test-report --tag="[cmd]"` passes (composed `formatOptionName` kept both call sites' math unchanged)
- [x] 2.3 Cover the collapsed-negation short-first rendering (`-p, --[no-]pack` in `cmd_cmd_tests.cpp`, `-y, --[no-]yes` in `cmd_config_tests.cpp`); the `--[no-]keep` needle in `cmd_help_tiering_tests.cpp` needs no change (keep has no short name; earlier wording here said `-p, --[no-]keep`, a planning typo)

## 3. Verification

- [x] 3.1 Run full suites: `xmake test-parallel` passes; visually check `encro -h`, `encro -hh`, `encro preview -h`, `encro config -h`, `encro completion -h` show short-first two-column alignment and `COLUMNS=72 xmake run encro -hh` still wraps within width
