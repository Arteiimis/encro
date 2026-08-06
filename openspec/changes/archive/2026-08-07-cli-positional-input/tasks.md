## 1. Failing tests first (RED)

- [x] 1.1 Add `cmd_cmd_tests.cpp` cases: single positional parses into positional input; multiple positionals parse as a list; positional mixed with `-i`/`-I` still parses at CLI11 level
- [x] 1.2 Add `cmd_config_builder_tests.cpp` cases: 1 positional → `inputPath` (file and dir, video/picture/pack-only); N positionals → `inputPaths`; N positionals + picture fails; N positionals + `-z` fails; positional + `-i`/`-I` conflict errors; N positionals containing a directory fails with the file-only message; no-input error mentions positional form

## 2. Parse layer (`src/cmd`)

- [x] 2.1 Add `positionalInputs` field to `CmdParseResult` in `cmd.h`
- [x] 2.2 Register optional positional option (`expected(0, N)`) on the IO group in `cmd.cpp` and map it into `positionalInputs` via `applyMap`
- [x] 2.3 Update the hardcoded usage lines in the help formatter to show the positional form (`encro <input>...`) alongside `-i`/`-I`
- [x] 2.4 Verify `-hh` renders the positional option under Input/Output options without a broken name column (adjust description/label if needed)

## 3. Config builder (`src/cmd/config_builder.cpp`)

- [x] 3.1 Map a single positional to `inputPath`, reusing the existing single-input validation
- [x] 3.2 Map two or more positionals to `inputPaths`, reusing the existing multi-input validation (video-only, files-only, no pack-only)
- [x] 3.3 Reject positional inputs combined with `-i`/`-I` with a conflict error mirroring the existing exclusion style
- [x] 3.4 Extend the no-input error to hint that a directory or file list can be passed directly

## 4. End-to-end coverage

- [x] 4.1 Add e2e test: `encro <dir>` encodes like `-i <dir>` (fake ffmpeg tooling)
- [x] 4.2 Add e2e test: `encro <a.mp4> <b.mp4>` encodes like `-I`
- [x] 4.3 Add e2e test: positional + `-i` fails with conflict error

## 5. Verification

- [x] 5.1 Run `xmake build tests && xmake run tests` (all tags pass, incl. `[cmd]`)
- [x] 5.2 Run `xmake build e2e_tests && xmake run e2e_tests`
- [x] 5.3 Run `xmake format -k check` on touched files
