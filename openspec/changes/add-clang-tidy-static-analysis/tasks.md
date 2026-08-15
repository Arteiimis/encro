# Tasks: Clang-Tidy Static Analysis

## 1. Check-set configuration

- [ ] 1.1 Create `.clang-tidy` at the repo root with the curated allowlist (clang-analyzer-*, bugprone-*, performance-*, portability-*, readability-function-size, readability-function-cognitive-complexity; minus bugprone-easily-swappable-parameters, performance-enum-size, portability-avoid-pragma-once) and thresholds `LineThreshold=80`, `cognitive-complexity.Threshold=25`
- [ ] 1.2 Verify the config loads and resolves: `clang-tidy --dump-config` exits 0 and shows the intended checks/options

## 2. Scan driver

- [ ] 2.1 Add `plugins/tidy/scan.py`: read `build/compile_commands.json`, scan each `.cpp` translation unit with `clang-tidy -p build`, aggregate warnings; default text output (file, line, message, check) plus a summary count; `--sarif` mode via `--export-fixes`; `-j` parallelism (default 16); header-filter `(^|[\\/])(src|tests)[\\/]`; non-zero exit on missing compile database or missing clang-tidy
- [ ] 2.2 Add a `--selftest` mode to `scan.py` that scans a bundled fixture containing a known violation and a `#pragma once` header, asserting the violation is flagged and the pragma-once check is not

## 3. xmake plugin

- [ ] 3.1 Add `plugins/tidy/xmake.lua`: a `tidy` task exposing `--sarif`, `-f` filter, and `-j` jobs, mirroring the `include-cleaner` plugin structure (find python3, exec `scan.py`, `assert` on driver failure)

## 4. Docs and verification

- [ ] 4.1 Document `xmake tidy` (and `--sarif`) in `CLAUDE.md` Build & Run
- [ ] 4.2 Run `xmake tidy` over the whole project and confirm warnings are scoped to `src/`/`tests/` only (no third-party noise), the summary count is reasonable, and `xmake tidy --selftest` passes
