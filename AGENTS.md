# AGENTS.md — encrō

encrō (encro) is a batch media processing CLI on top of ffmpeg: parallel video transcoding (HEVC/H.264), picture → WebP/JPEG recompression, ZIP packing, a resumable job store, and an original-vs-encoded `preview` subcommand.

## Build & Run

- **Build system:** xmake (not CMake). Toolchain: `clang-cl` + `lld-link` on Windows. C++26.
- **Build:** `xmake build encro` · **Run:** `xmake run encro <args>` (e.g. `xmake run encro -h`; do NOT use `--` — it is passed through to the program and breaks CLI11 parsing)
- **Tests with failure summary:** `xmake test-report` — builds + runs unit tests, writes `build/last-test-report.xml` (JUnit) and `build/last-test-console.log` (full console text), and prints a summary instead of raw console: success shows `All tests passed (N assertions in M test cases)`, failure prints a per-test FAILED list (name + file:line + message) plus both artifact paths. `--tag="[tag]"` limits to a tag filter (note: `=` form required).
- **Tests (e2e):** `xmake build e2e_tests && xmake run e2e_tests` (needs `encro` + `encro_e2e_tool` fake ffmpeg/ffprobe built first)
- **Tests (parallel):** `xmake test-parallel` — builds then runs the unit suite in 8 Catch2 shards and the e2e suite in 4 shards concurrently (real-ffmpeg tests included). Each shard gets an isolated temp root under `build/.test-parallel/`, so shared scratch/TempDir/log state never collides. Shard counts auto-scale from CPU count (`--unit-shards=N` / `--e2e-shards=N` override). Success/failure is judged from the Catch2 logs (parallel `proc:wait` statuses are unreliable); failed shard logs are printed with paths under `build/.test-parallel/`.
- **Format:** `xmake fmt` (apply) / `xmake fmt -k` (check only, no CI gate). Default style `file:D:/clangformat/.clang-format` (not in repo); `--style` overrides it. Full options: `xmake fmt -h`.
- **Static analysis:** `xmake tidy` — report-only clang-tidy over `src/`+`tests/` (`.clang-tidy` config, function-length/cognitive-complexity guardrails); needs `build/compile_commands.json` (build first). Full options: `xmake tidy -h`.
- **Coverage:** `xmake coverage` runs tests under coverage with an instrumentation self-check, then restores release; needs `llvm-profdata` + `llvm-cov` on PATH. Full options: `xmake coverage -h`.
- **Size:** `xmake size` prints section sizes (llvm-size); `-d` adds per-object breakdown via PDB (auto-rebuilds with debug info if missing). Full options: `xmake size -h`.
- **ASan:** `xmake f -m releasedbg && xmake build encro` (config then build; `xmake f` alone only reconfigures)
- **Dependency headers:** read `build/compile_commands.json` for absolute include paths — they live there, not in the repo (never search `~/.xmake`).

### Build Modes

| Mode         | Flags                                                  |
| ------------ | ------------------------------------------------------ |
| `debug`      | ASan off; all log levels kept                          |
| `release`    | LTO; TRACE/DEBUG stripped (`SPDLOG_ACTIVE_LEVEL`)      |
| `releasedbg` | Optimized + debug info + ASan                          |
| `coverage`   | `-fprofile-instr-generate -fcoverage-mapping`          |

## Code Conventions (clang-format enforces layout)

| Item           | Rule                                                                 |
| -------------- | -------------------------------------------------------------------- |
| East const     | `std::string const&`                                                 |
| Trailing return| Simple scalar types and `void` use prefix style (`bool f()`, `int main()`); trailing `auto f(...) -> T` for complex/derived types only. Lambdas keep explicit return types when deduction would change them (e.g. mixed `return 0;`/`uint64_t`). |
| Naming         | Files `snake_case` · Types `PascalCase` · Functions `camelCase`      |
| Members        | `camelCase` + trailing `_` (e.g., `stateFilePath_`)                  |
| Constants      | `k` + `PascalCase` (e.g., `kEncodeVideoKind`)                        |
| Namespaces     | lowercase, no separators, no indent inside                           |
| Header guards  | `#pragma once` only                                                  |
| Include order  | own header → project headers by module → third-party → stdlib; relative to `src/` |
| Comments       | Minimal, no Doxygen                                         |
| Template params| `Ty` (single), `Tys` (pack)                                          |

## Testing

- Catch2 v3 (`catch2/catch_all.hpp`), custom runner `tests/test_main.cpp`.
- Fixtures/helpers in `tests/test_utils.h`. `TempDir` keeps its directory (and prints the path to stderr) when a test fails, so state files / fake-tool logs survive for inspection. E2E subprocess failures dump child stdout/stderr via `REQUIRE_SUCCESS` (in `tests/e2e/e2e_test_utils.h`).
- E2E: `fake_media_tool.cpp` impersonates ffmpeg/ffprobe, controlled via env vars (`ENCRO_FAKE_FFMPEG_EXIT_CODE`, ...).
- `[real-ffmpeg]`/`[smoke]` tests auto-skip via `SKIP()` when ffmpeg not on PATH.
- Compile-only tests (`pack_api_standalone_compile_test.cpp`, `packer_standalone_compile_test.cpp`): `static_assert` verifies API boundaries.
- Tags: `[job-state]`, `[cmd]`, `[pack-service]`, `[packer]`, `[video-info]`, `[e2e]`, ...

## Communication

- 与用户对话使用中文（代码注释、git 提交、OpenSpec 文档仍为英文）。

## Development Workflows

- **OpenSpec:** features follow proposal → specs → design → tasks → implementation via the `openspec-*` skills (explore → propose → apply/update → sync → archive; artifacts in `openspec/changes/`). The workflow is mirrored per harness — read the copy under the current harness's skills directory (`.pi/skills/`, `.opencode/skills/`, `.zcode/skills/`); copies may differ slightly to match each harness's command syntax. **Before starting any OpenSpec step (explore, propose, apply, update, sync, archive), read the corresponding skill first (`<harness-dir>/skills/openspec-<step>/SKILL.md`) and follow its workflow exactly** — never run an OpenSpec step from memory. Every feature needs ≥1 test. **All OpenSpec/spec documents (proposal, specs, design, tasks) must be written in English.**
- **OpenSpec review timing:** write ALL planning artifacts (proposal → specs → design → tasks) before reviewing the proposal against the complete set — a proposal review without its specs/design/tasks cannot validate the contract between them.
- **TDD:** never write implementation before tests; test + implementation go in the same commit.
- **Post-Change Review:** after self-verification, unless trivial (typos, docs-only, one-liner or mechanical refactor), run the `code-review` skill (spec from `openspec/changes/`; if the current harness has no copy, use another harness's, e.g. `.pi/skills/code-review/`); for large multi-area changes add ≤1 sub-agent per functional area for edge cases only; report severity + file:line findings, triage, fix, re-verify.

## Platform & Git

- **Platform:** primary Windows clang-cl (`NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_MSVC_STL_HARDENING=1`); POSIX paths via `generic_string()` in `src/core/collision_naming.h`, `src/pack`, `src/picture`. External ffmpeg/ffprobe discovered via PATH or `--ffmpeg-path`.
- **Commits:** English only (no CJK in git metadata); conventional commits (`feat:`/`fix:`/`docs:`/`test:`/`refactor:`/`chore:`), subject <72 chars; batch large working trees by functional area.
  - **OpenSpec planning artifacts** (proposal/specs/design) are committed *before* implementation, as their own `docs:` commit — they describe what will be built, not the build itself.
  - **Implementation + its tests + the change's `tasks.md` checkboxes go in ONE commit** (atomic: `git revert` removes the feature and its completion state together; no "code gone but tasks still checked" intermediate state).
  - A code change and its documentation belong in the same commit when they tell one story; split only when the docs are a prerequisite (planning) or an independent deliverable (user guide).
- **Pre-commit hook:** clang-format on staged C/C++ files (`.githooks/pre-commit`; setup `git config core.hooksPath .githooks`).
