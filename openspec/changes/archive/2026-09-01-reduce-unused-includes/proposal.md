## Why

The ponytail audit (2026-08-31) flagged `plugins/include_cleaner` as the only zero-reference plugin, but the cleanup was deferred: include-usage analysis was suspected to differ between Windows (clang-cl/MSVC STL) and Linux (clang/libstdc++), and deleting an include that only one platform considers unused would break the other. That concern has now been verified empirically — and the difference is real. Full scans with `clang-include-cleaner` on the current branch (3b9c859) report 111 unused includes on Windows vs 118 on Linux, with only a 100-item intersection. Platform-divergent findings (29 items) are almost exclusively platform headers (`<windows.h>`, `<io.h>`, `<boost/stacktrace.hpp>`) or transitive-include artifacts (`<array>`, `<spdlog/logger.h>`) — exactly the class of include that must be kept. The intersection is safe to delete on both platforms, and the divergent items can be annotated so future scans stop re-reporting them.

## What Changes

- Delete the 100 unused includes that BOTH platform scans agree on (the intersection, saved at `build/include_cleaner_intersection.txt`). No code changes — include lines only.
- Annotate the 29 platform-divergent includes with `// IWYU pragma: keep` plus a one-line reason, so `xmake include-cleaner --check` stops reporting them as unused on the platform that flags them.
- After the change, `xmake include-cleaner` reports **zero** unused includes on both Windows and Linux, which makes a future CI gate (`include-cleaner --check` on the matrix) feasible.
- No behavior change: headers still reachable, builds must stay green on both platforms.

## Capabilities

### New Capabilities

None. This is a pure refactor of include lines; no user-visible or spec-level behavior changes (`skip_specs: true`, same as `remove-immer-simplify-locks` and `reduce-over-engineering`).

### Modified Capabilities

None.

## Impact

- ~50 files across `src/` and `tests/` (one include line each for the 100 deletions; 29 annotated lines).
- Build: marginally faster (fewer headers parsed); no dependency changes.
- Tooling: `plugins/include_cleaner` gains its first real consumer (was zero-reference); `xmake include-cleaner --check` becomes a viable CI check afterward (adding the CI step itself is out of scope).
- Verification requires both platforms: Windows build + `xmake test-report` + include-cleaner scan; Linux (WSL, clang 19) build + scan. Linux WSL copy at `~/encro` is already synced and built.
- Reversal of the archived `2026-08-28-test-suite-cleanup` audit decision that deferred include_cleaner work; the deferral reason (platform divergence) is now measured, and the intersection strategy addresses it.

## Summary

Applied 2026-09-01 on both platforms: the 100-intersection includes were deleted and the 29 platform-divergent includes annotated with `IWYU pragma: keep`. Both platforms now verify clean — `xmake include-cleaner --check` passes with zero unused includes on Windows and Linux, so the check is CI-gateable on both platforms. Adding the CI step itself remains a separate change (out of scope here).
