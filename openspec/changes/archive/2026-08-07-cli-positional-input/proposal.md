## Why

Typing `encro -i dir` or `encro -I a.mp4 b.mp4` is boilerplate for the most common invocation: the input is the primary argument of the tool. Users expect `encro dir` and `encro a.mp4 b.mp4` to just work, matching how most CLI tools accept positional inputs.

## What Changes

- `encro <path>` accepts one positional input path (file or directory), behaving exactly like `-i/--input` in every mode (video, picture, pack-only).
- `encro <path>...` accepts multiple positional input paths, behaving exactly like `-I/--inputs`: video mode only, regular files only, incompatible with `-z/--pack-only`.
- Mixing positional inputs with `-i/--input` or `-I/--inputs` is a hard error, mirroring the existing `-i`/`-I` mutual exclusion.
- A single positional input that is a directory works with `-z/--pack-only` and picture mode (via the `-i` path), so `encro dir -z` works.
- Multiple positional inputs containing a directory produce a clear error explaining that multiple inputs must be files.
- Help text updates: usage lines gain the positional form; the positional option is documented in the IO group.
- No-args error message gains a hint about passing a directory or file list.
- Files starting with `-` can be passed after `--` (same limitation `-I` has today; not a regression).

## Capabilities

### New Capabilities

- `cli-positional-input`: behavior of positional input paths as an alternative to `-i/--input` and `-I/--inputs`, including mapping rules, conflict rules, and error messages.

### Modified Capabilities

<!-- None: existing specs do not describe input flag behavior at requirement level. -->

## Impact

- `src/cmd/cmd.cpp`: register positional option, update usage lines and IO group help.
- `src/cmd/config_builder.cpp`: map 1 positional → `inputPath`, N positionals → `inputPaths`, reject mixing with `-i`/`-I`, refine error messages.
- `src/cmd/cmd.h`: add positional field to `CmdParseResult`.
- Tests: `tests/cmd_cmd_tests.cpp`, `tests/cmd_config_builder_tests.cpp`, e2e.
- No pipeline changes (`src/app/pipeline.cpp` untouched).
