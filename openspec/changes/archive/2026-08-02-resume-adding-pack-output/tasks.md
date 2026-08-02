## 1. Core Implementation

- [x] 1.1 Make `packOutput` comparison directional in `jobstate::configMatches` (src/core/job_state.cpp): configs match when `packOutput` is equal, or when the saved state has `packOutput=false` and the new run has `packOutput=true`
- [x] 1.2 Verify `Store::initialize` resume path (src/core/job_state_store.cpp) loads the snapshot when the directional rule matches, and keeps existing mismatch handling for all other cases

## 2. Tests (TDD)

- [x] 2.1 Write failing test: saved config with `packOutput=false` matches a new config with `packOutput=true` (all other fields equal)
- [x] 2.2 Write failing test: saved config with `packOutput=true` does NOT match a new config with `packOutput=false`
- [x] 2.3 Write failing test: `packOutput=false -> true` still fails to match when another field (e.g., outputFormat) differs
- [x] 2.4 Run `xmake build tests && xmake run tests "[job-state]"` to confirm the new tests fail (RED)
- [x] 2.5 Implement the directional comparison (1.1) and confirm the new tests pass (GREEN)
- [x] 2.6 Add integration-style coverage in tests/video/video_process_orchestration_tests.cpp: encode-only run then pack-enabled run reuses recovered encodes and proceeds to packing (or verify existing orchestration test covers the matched path)

## 3. Verification

- [x] 3.1 Run the full unit/integration suite: `xmake build tests && xmake run tests`
- [x] 3.2 Run `xmake format -k check` to verify formatting
- [x] 3.3 Manual smoke (optional): encode a small dir without `--pack-output`, re-run with `--pack-output`; confirm completed encodes are skipped and packing proceeds
