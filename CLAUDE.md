# AGENTS.md — encrō

encrō (encro) is a batch media processing CLI on top of ffmpeg: parallel video transcoding (HEVC/H.264), picture → WebP/JPEG recompression, ZIP packing, a resumable job store, and an original-vs-encoded `preview` subcommand.

## Build & Run

- **Build system:** xmake (not CMake). Toolchain: `clang-cl` + `lld-link` on Windows. C++26.
- **Build:** `xmake build encro` · **Run:** `xmake run encro <args>` (e.g. `xmake run encro -h`; do NOT use `--` — it is passed through to the program and breaks CLI11 parsing)
- **Tests (unit/integration):** `xmake build tests && xmake run tests`
- **Tests with failure summary:** `xmake test-report` — builds + runs unit tests, writes `build/last-test-report.xml` (JUnit) and `build/last-test-console.log` (full console text), and prints a summary instead of raw console: success shows `All tests passed (N assertions in M test cases)`, failure prints a per-test FAILED list (name + file:line + message) plus both artifact paths. Child stdout goes to the log file, so progress-bar frames never render (non-TTY) and the terminal output stays clean in every context; exit code is propagated. `--tag="[tag]"` limits to a tag filter (note: `=` form required).
- **Tests (e2e):** `xmake build e2e_tests && xmake run e2e_tests` (needs `encro` + `encro_e2e_tool` fake ffmpeg/ffprobe built first)
- **Single test:** `xmake run tests "[tag-name]"` (Catch2 tag filter)
- **Format:** `xmake fmt` (apply) / `xmake fmt -k` (check only, no CI gate). Default style `file:D:/clangformat/.clang-format` (NOT in repo); `--style` overrides it. Full options: `xmake fmt -h`.
- **Static analysis:** `xmake tidy` — report-only clang-tidy over `src/`+`tests/` (`.clang-tidy` config, function-length/cognitive-complexity guardrails); needs `build/compile_commands.json` (build first). Full options: `xmake tidy -h`.
- **Coverage:** `xmake coverage` runs tests under coverage with an instrumentation self-check, then restores release; needs `llvm-profdata` + `llvm-cov` on PATH. Full options: `xmake coverage -h`.
- **Size:** `xmake size` prints section sizes (llvm-size); `-d` adds per-object breakdown via PDB (auto-rebuilds with debug info if missing). Full options: `xmake size -h`.
- **ASan:** `xmake f -m releasedbg && xmake build encro` (config then build; `xmake f` alone only reconfigures)
- **Dependency headers:** read `build/compile_commands.json` for absolute include paths (e.g., `F:\xmake\.xmake\packages\i\indicators\2.3\<hash>\include`). They live there, NOT in the repo — never search `~/.xmake`.

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
| Trailing return| `auto fn(...) -> ReturnType` on all functions, incl. `main`          |
| Naming         | Files `snake_case` · Types `PascalCase` · Functions `camelCase`      |
| Members        | `camelCase` + trailing `_` (e.g., `stateFilePath_`)                  |
| Constants      | `k` + `PascalCase` (e.g., `kEncodeVideoKind`)                        |
| Namespaces     | lowercase, no separators, no indent inside                           |
| Header guards  | `#pragma once` only                                                  |
| Include order  | own header → project headers by module → third-party → stdlib; relative to `src/` |
| Comments       | Minimal, no Doxygen; code is self-documenting                        |
| Template params| `Ty` (single), `Tys` (pack)                                          |

## Architecture

```
main → appentry::run → prelude::initStartup → pipeline::run
         ├─ runVideo() → video_process → video_batch_execution → video_encode_runner
         ├─ runPicture() → picture_process → picture_compress
         ├─ runPackOnly() → pack::execute()
         └─ preview subcommand → preview::run → preview_process (bypasses pipeline::run)
```

- `src/app` CLI entry, startup, pipeline orchestration · `src/cmd` CLI11 parsing, config builder
- `src/core` AppContext, JobState, TaskExecutor, Progress, media_scanner, error_handle, collision_naming, display_text, parallel, path_roots
- `src/infra` crash handler, terminal, console_width, env, stacktrace, stop_signal, toolchain discovery
- `src/logging` spdlog setup (async, rotating), JSON formatter, log tags
- `src/pack` ZIP (PackRequest → PackPlan → Packer → libzippp) · `src/picture` ffmpeg WebP
- `src/preview` original-vs-encoded side-by-side comparison (preview_process, preview_filtergraph)
- `src/video` encode orchestration, progress parsing, output planning · `src/utils` exec2, FFmpeg discovery, UUID

- **`appctx::AppContext`** — single mutable context, passed by mutable reference through the whole chain.
- **`eh::Result<T>`** = `std::expected<T, std::string>`; all operational failures return it. Exceptions only for catastrophic errors (caught in `main.cpp`).
- **`jobstate::Store`** — persists task records as JSON for resume after interruption.

## Error Handling

```cpp
return eh::makeError("Failed to open state file: {}", path.string());
if (!result) { return eh::makeError("context: {}", result.error()); }  // REQUIRE(result) in tests
```

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

- **OpenSpec:** features follow proposal → specs → design → tasks → implementation via `.pi/skills/openspec-*` (explore → propose → apply/update → sync → archive; artifacts in `openspec/changes/`). **Before starting any OpenSpec step (explore, propose, apply, update, sync, archive), read the corresponding skill first (`.pi/skills/openspec-<step>/SKILL.md`) and follow its workflow exactly** — never run an OpenSpec step from memory. Every feature needs ≥1 test. **All OpenSpec/spec documents (proposal, specs, design, tasks) must be written in English.**
- **TDD:** 1) write failing test first (RED) 2) minimal code to pass (GREEN) 3) refactor under test protection 4) verify all pass. Never write implementation before tests; test + implementation go in the same commit.
- **Post-Change Review:** after self-verification, unless the change is trivial (typos, docs-only, one-liner or mechanical refactor), run the `code-review` skill (parallel Standards + Spec axis sub-agents; spec from `openspec/changes/`); for large multi-area changes additionally launch ≤1 sub-agent per touched functional area to review edge cases only (empty inputs, unusual codecs, exit codes, concurrency, stop-signal paths); report severity + file:line findings; triage and fix, then re-run full verification.

## Key Dependencies

- **CLI11** CLI parsing · **Boost** json/parser/lambda2/lexical_cast/process/stacktrace/uuid · **spdlog** async logging to `%LOCALAPPDATA%/encro/logs/` (rotating, keep 10) · **fmt** formatting · **libzippp** ZIP I/O · **immer** immutable structures · **indicators** progress bars · **thread-pool** parallel execution

## Platform & Git

- **Platform:** primary Windows clang-cl (`NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_MSVC_STL_HARDENING=1`); POSIX paths via `generic_string()` in `src/core/collision_naming.h`, `src/pack`, `src/picture`. External ffmpeg/ffprobe discovered via PATH or `--ffmpeg-path`.
- **Commits:** English only (no CJK in git metadata); conventional commits (`feat:`/`fix:`/`docs:`/`test:`/`refactor:`/`chore:`), subject <72 chars; batch large working trees by functional area.
  - **OpenSpec planning artifacts** (proposal/specs/design) are committed *before* implementation, as their own `docs:` commit — they describe what will be built, not the build itself.
  - **Implementation + its tests + the change's `tasks.md` checkboxes go in ONE commit** (atomic: `git revert` removes the feature and its completion state together; no "code gone but tasks still checked" intermediate state).
  - A code change and its documentation belong in the same commit when they tell one story; split only when the docs are a prerequisite (planning) or an independent deliverable (user guide).
- **Pre-commit hook:** clang-format on staged C/C++ files (`.githooks/pre-commit`; setup `git config core.hooksPath .githooks`).
