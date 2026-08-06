## Context

Current CLI (`src/cmd/cmd.cpp`) registers only `-i/--input` (single path, file or dir, all modes) and `-I/--inputs` (multi file, video mode only, no pack-only). There is no positional option, so bare arguments fail CLI11 parsing with an extras error. The pipeline (`src/app/pipeline.cpp`) already branches on `inputPath` vs `inputPaths`, so no pipeline change is needed if positionals map onto those two fields.

## Goals / Non-Goals

**Goals:**
- Positionals map onto the existing `inputPath`/`inputPaths` contract with zero pipeline changes.
- Conflicts with `-i`/`-I` reuse the existing mutual-exclusion error style.

**Non-Goals:**
- Multiple directories in one invocation (multi-dir needs pipeline work in video/picture/pack flows; see proposal - the `-I` file-only rule is preserved).
- Directory expansion inside a multi-input list.
- Glob expansion (platform shell's job, same as `-I` today).

## Decisions

### D1: One positional → `inputPath`, N positionals → `inputPaths`

The config builder maps a single positional to `inputPath` (inheriting `-i` semantics: file or dir, video/picture/pack-only) and two or more to `inputPaths` (inheriting `-I` semantics: video only, files only, no pack-only). This is a pure mapping — every existing validation in `buildConfig` applies unchanged.

Alternative considered: a dedicated "mixed list" pipeline path. Rejected: duplicates `-I` logic and requires pipeline changes for a fringe case.

### D2: CLI11 registers one optional positional option

`add_option("inputs", ...)` with `expected(0, N)` — one option, `std::vector<std::string>`. No `positionals_at_end` (mixed ordering like `encro a.mp4 -o out` works natively). `--` remains the escape hatch for dash-prefixed names, same as `-I` today.

The positional is registered on the IO group so the custom help formatter renders it under "Input/Output options" (the formatter only surfaces app-level options under the general group).

### D3: Conflict detection in `buildConfig`

CLI11 has no `excludes` relationship for positionals, so mixing is detected in `buildConfig` where `-i`/`-I` exclusivity is already checked: presence of positionals alongside either flag is an error mirroring the existing message style. Parse-level (`cmd.cpp`) stays a passive collector of the positional vector.

### D4: New `CmdParseResult` field

Add `std::optional<std::vector<std::string>> positionalInputs` to `CmdParseResult`; the parse maps the positional option into it. Keeps the data-driven `applyMap` pattern intact.

### D5: Error message polish

- Multi-positional containing a directory: reuse the `-I` "not a regular file" validation but with a message that explains the positional context (multiple inputs must be files; run once per directory).
- No-input error: extend "Input path is required." with a hint that a directory or file list may be passed directly.

## Risks / Trade-offs

- A user who typo's a flag (e.g. `encro dir --recursiv`) previously got a parse error; now the unknown flag still errors but any stray bare word becomes a positional — the "both positional and -i" error should catch the common `encro -i a b` case. Acceptable; matches `-I` precedent.
- Help layout: the positional option has no flag names, so `formatOptionName` renders an empty name column — verify the IO-group rendering doesn't look broken; fallback is a short synthetic label in the description.
