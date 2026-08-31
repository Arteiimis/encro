## 1. Preparation

- [ ] 1.1 Re-run both platform scans on the apply branch (Windows: `python plugins/include_cleaner/scan.py -j 16`; WSL: `~/bin/clang-include-cleaner` via scan.py on synced `~/encro`), recompute the fresh intersection, and diff against `build/include_cleaner_intersection.txt` (expected: same 100 items; report any drift)

## 2. Implementation

- [ ] 2.1 Delete the 100 intersection includes (one include line per file, ~50 files across `src/` and `tests/`); keep any item that is alive at apply time and report it
- [ ] 2.2 Annotate the 29 platform-divergent includes with `// IWYU pragma: keep -- <reason>` (11 Windows-only items: platform/transitive deps e.g. `<windows.h>`, `<io.h>`; 18 Linux-only items: `<spdlog/logger.h>`, `<memory>`, `<chrono>`, `<boost/stacktrace.hpp>`, `"infra/crash_runtime.h"`, ...)

## 3. Verification

- [ ] 3.1 Windows: `xmake build` + `xmake test-report` (all green) + `xmake include-cleaner` reports `=== unused includes: 0 ===`
- [ ] 3.2 Linux: sync `~/encro` to the apply branch, `xmake build` (regenerates compile_commands.json), `xmake include-cleaner` reports `=== unused includes: 0 ===`
- [ ] 3.3 Commit as one `refactor:` commit (English, conventional), ticking this tasks.md in the same commit

## 4. Follow-up (out of scope, noted)

- [ ] 4.1 Record in the change summary that `xmake include-cleaner --check` is now CI-gateable on both platforms (adding the CI step itself is a separate change)
