# AGENTS.md — encrō

encrō (encro) is a batch media processing CLI on top of ffmpeg: parallel video transcoding (HEVC/H.264), picture → WebP/JPEG recompression, ZIP packing, a resumable job store, and an original-vs-encoded `preview` subcommand.

## Build & Run

- **Build system:** xmake (not CMake). Toolchain: `clang-cl` + `lld-link` on Windows. C++26. For any xmake-related question or change (xmake.lua, configuration, CLI), read the `xmake` skill first.
- **Build:** `xmake build encro` · **Run:** `xmake run encro <args>` (e.g. `xmake run encro -h`; do NOT use `--` — it is passed through to the program and breaks CLI11 parsing)
- **Tests with failure summary:** `xmake test-report` — builds + runs unit tests, writes `build/last-test-report.xml` (JUnit) and `build/last-test-console.log` (full console text), and prints a pass/fail summary instead of raw console. `--tag="[tag]"` limits to a tag filter (note: `=` form required).
- **Tests (e2e):** `xmake build e2e_tests && xmake run e2e_tests` (needs `encro` + `encro_e2e_tool` fake ffmpeg/ffprobe built first)
- **Tests (parallel):** `xmake test-parallel` — unit + e2e suites in parallel shards (real-ffmpeg tests included; `--unit-shards=N` / `--e2e-shards=N` override shard counts). Judge success/failure from the Catch2 logs only (parallel `proc:wait` statuses are unreliable); failed shard logs land under `build/.test-parallel/`.
- **Format:** `xmake fmt` (apply) / `xmake fmt -k` (check only, no CI gate). Default style `file:D:/clangformat/.clang-format` (not in repo); `--style` overrides it.
- **Static analysis:** `xmake tidy` — report-only clang-tidy over `src/`+`tests/` (`.clang-tidy` config, function-length/cognitive-complexity guardrails); needs `build/compile_commands.json` (build first).
- **Coverage:** `xmake coverage` runs tests under coverage with an instrumentation self-check, then restores release; needs `llvm-profdata` + `llvm-cov` on PATH.
- **Size:** `xmake size` prints section sizes (llvm-size); `-d` adds per-object breakdown via PDB (auto-rebuilds with debug info if missing).
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

- Fixtures/helpers in `tests/test_utils.h`. `TempDir` keeps its directory (and prints the path to stderr) when a test fails, so state files / fake-tool logs survive for inspection. E2E subprocess failures dump child stdout/stderr via `REQUIRE_SUCCESS` (in `tests/e2e/e2e_test_utils.h`).
- E2E: `fake_media_tool.cpp` impersonates ffmpeg/ffprobe, controlled via env vars (`ENCRO_FAKE_FFMPEG_EXIT_CODE`, ...).
- `[real-ffmpeg]`/`[smoke]` tests auto-skip via `SKIP()` when ffmpeg not on PATH.
- `[install]`/`[smoke]` completion tests (install/uninstall writes shell startup files; smoke spawns real bash/PowerShell) are opt-in: skipped unless `ENCRO_TEST_COMPLETION=1`; run manually with `ENCRO_TEST_COMPLETION=1 xmake test-report --tag="[completion]"`.
- Tests carry tags for `--tag=` filtering — see the `[...]` annotations in `tests/`.
- **Sync convention:** tests synchronize by polling observable state via `testutils::waitUntil` (invocation logs, gate files, mutex-guarded fields) — never fixed sleeps, and no negative assertions that race async effects. Any `sleep_for` in `tests/**.cpp` needs a `// sleep-ok: <reason>` marker within 3 lines (pre-commit clang-format reflows long lines); enforced by the `[test-utils][meta]` check in `tests/test_utils_tests.cpp`. Exempt: `tests/e2e/fake_media_tool.cpp`, `tests/e2e/e2e_test_utils.cpp`. Fake-tool holds: `ENCRO_FAKE_FFMPEG_GATE_FILE` (+ `ENCRO_FAKE_FFMPEG_GATE_FROM_CALL=N` to gate only the Nth-and-later invocation); job-state elapsed tests drive `testutils::ScopedSyntheticJobClock` instead of the wall clock.

## Communication

- 与用户对话使用中文（代码注释、git 提交、OpenSpec 文档仍为英文）。

## Development Workflows

- **OpenSpec:** features follow proposal → specs → design → tasks → implementation via the `openspec-*` skills (artifacts in `openspec/changes/`). Skills are mirrored per harness — use the current harness's copy (`.pi/skills/`, `.opencode/skills/`, `.zcode/skills/`). **Before starting any OpenSpec step, read the corresponding skill first (`<harness-dir>/skills/openspec-<step>/SKILL.md`) and follow its workflow exactly — never run an OpenSpec step from memory.** Every feature needs ≥1 test; spec documents are written in English (see Communication).
- **OpenSpec explore trigger:** when the user's request is exploratory — "探索下"/"explore", feasibility or options discussion, or similar exploratory intent — automatically start with the `openspec-explore` skill (read `<harness-dir>/skills/openspec-explore/SKILL.md` and enter explore mode) instead of answering or implementing directly.
- **OpenSpec review timing:** write all planning artifacts (proposal → specs → design → tasks) before reviewing the proposal against the complete set — a proposal review without its specs/design/tasks cannot validate the contract between them. Run the proposal review as a fresh sub-agent against the written artifacts (not the author's intent), then apply the review fix loop below.
- **OpenSpec archive auto-sync:** when archiving a change whose delta specs are not yet applied to the main specs, run the sync step automatically (inline, as the archive skill prescribes) without prompting; never archive with stale main specs. Skip the sync only when the user explicitly says so.
- **TDD:** never write implementation before tests; test + implementation go in the same commit.
- **Post-Change Review:** after self-verification, unless trivial (typos, docs-only, one-liner or mechanical refactor), run the `code-review` skill (spec from `openspec/changes/`; if the current harness has no copy, use another harness's, e.g. `.pi/skills/code-review/`) with a third leanness sub-agent alongside Standards/Spec, using ponytail-review criteria (correctness/security/performance out of scope — other axes own those; never flag the mandated test or tooling-enforced rules). Sub-agents don't inherit skills or the author's ponytail mode — paste the tag rules from the `ponytail-review` skill verbatim into its brief. For large multi-area changes add ≤1 sub-agent per functional area for edge cases only; report severity + file:line findings, triage, fix, re-verify per the review fix loop below.
- **Review fix loop** (proposal review and code review alike): the agent that wrote the fix never grades it alone. After fixing the accepted findings, spawn a fresh verification sub-agent with the findings list + the fix diff; it returns a per-finding verdict — resolved / not resolved / regressed (the fix broke something else). Loop until every finding resolves; hard cap 2 fix→verify rounds, then stop and hand unresolved findings — plus anything rejected during triage, with justification — to the user instead of looping.

## Platform & Git

- **Platform:** primary Windows clang-cl (`NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_MSVC_STL_HARDENING=1`); POSIX paths via `generic_string()` in `src/core/collision_naming.h`, `src/pack`, `src/picture`. External ffmpeg/ffprobe discovered via PATH or `--ffmpeg-path`.
- **Commits:** English only (no CJK in git metadata); conventional commits (`feat:`/`fix:`/`docs:`/`test:`/`refactor:`/`chore:`), subject <72 chars; batch large working trees by functional area.
  - OpenSpec planning artifacts (proposal/specs/design) are committed before implementation, as their own `docs:` commit — they describe what will be built, not the build itself.
  - Implementation + its tests + the change's `tasks.md` checkboxes go in one commit (atomic: `git revert` removes the feature and its completion state together; no "code gone but tasks still checked" intermediate state).
  - A code change and its documentation belong in the same commit when they tell one story; split only when the docs are a prerequisite (planning) or an independent deliverable (user guide).
- **Pre-commit hook:** clang-format on staged C/C++ files (`.githooks/pre-commit`; setup `git config core.hooksPath .githooks`).
