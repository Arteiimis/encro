# Tasks

## 1. Generated commit stamp

- [ ] 1.1 Add `${define ENCRO_GIT_COMMIT}` to `src/app/version.h.in` and enable `set_policy("build.always_update_configfiles", true)` in `xmake.lua`. Verify: `xmake f -y` renders `build/app/version.h` with `ENCRO_VERSION` and a commit equal to `git rev-parse --short HEAD`; after `git commit --allow-empty`, a plain `xmake build` (no `xmake f`) rewrites the commit to the new HEAD and touches nothing outside `build/`; drop the scratch commit with `git reset --hard HEAD~1` afterwards.

## 2. Tests first

- [ ] 2.1 Rewrite `tests/app/version_tests.cpp` to assert the captured `--version` output equals `encro v<ENCRO_VERSION>[+<ENCRO_GIT_COMMIT>]` followed by a newline, and contains no `build:` clause. Verify: the case fails against the current binary, which still prints `encro 2.0.3 (build: ...)`.
- [ ] 2.2 In `tests/app/app_entry_tests.cpp`, replace the build-timestamp case with an exact-equality assertion: `appentry::helpIntroLine()` equals `encro: Universal video encoder/converter/packer | v<ENCRO_VERSION>`, followed by `+<ENCRO_GIT_COMMIT>` unless that macro is empty or `"none"` (the file needs `#include "app/version.h"`). Verify: the case fails by assertion against the current banner, not by a compile error.

## 3. Implementation

- [ ] 3.1 Add the token's own cases to `tests/app/app_entry_tests.cpp` first - a present commit, an empty commit and `"none"` - then declare `appentry::versionToken(std::string_view version, std::string_view commit)` in `src/app/app_entry.h` and implement it in `app_entry.cpp` (prefix `v`, append `+<commit>` unless the commit is empty or `"none"`); compose `helpIntroLine()` and the `--version` line with it; delete `compileTimestamp()`, `parseDecimal()` and `monthNumber()`. Verify: the new cases are red (unresolved symbol) before the declaration and green after it; 2.1 and 2.2 pass; `xmake run encro -h`'s first line reads `encro: Universal video encoder/converter/packer | v2.0.3+<hash>` and `xmake run encro --version` reads `encro v2.0.3+<hash>`, neither containing a date or a time of day.

## 4. Documentation

- [ ] 4.1 Update the two manual `encro --version` checks in `.agents/skills/release/SKILL.md` (downloaded archive, installed binary) to expect the `v`-prefixed token with its commit suffix. Verify: running the documented command against the freshly built binary prints exactly the documented shape.

## 5. Integration checks

- [ ] 5.1 `xmake test-report --suite=all` is green, and the reporter-mode probe `build/windows/x64/release/tests.exe -r console -s` reports 0 failures.
- [ ] 5.2 With the implementation committed and the worktree clean, `git commit --allow-empty -m "tmp: commit refresh"` then `xmake build encro` makes the banner report the new hashed commit; `git reset --hard HEAD~1` afterwards returns the tree to its committed state. Verify: the printed token changes with the commit and `git status` is clean at the end.

## Planning-artifact review

One fresh reviewer ran the `code-review` skill's planning stage (Coherence, then Ground truth) over proposal, both delta specs, design and tasks, and a second fresh reviewer verified the fixes finding by finding. All seven findings are resolved; none was rejected.

1. [P1][Coherence] The version-numbering delta pinned `--version` two incompatible ways - the retained scenario promised the bare output `v2.0.0` while the added requirement appends `+<short commit>` - resolved (planning review): the scenario now says the version value is `v2.0.0`, followed only by build metadata when the build carries a commit.
2. [P2][Coherence] Design D6 kept the token composition file-local while task 3.1 required an exported `appentry::versionToken` - resolved (planning review): D6 now declares it in `app_entry.h` beside `helpIntroLine()` and states why (the no-commit cases exist only in a git-less build, so the arguments are the only cheap test seam).
3. [P2][Coherence] The proposal claimed a `cli-help-layout` requirement *changes*, but that spec has no header requirement and the delta correctly adds one - resolved (planning review): the capability line now says it gains a requirement no existing one covers.
4. [P2][Ground truth] The claim that the Scoop manifest uses the `v` prefix was false (`.github/workflows/release.yml:77` strips it) - resolved (planning review, re-checked after the first fix missed the design copy): proposal and design now say the release tag carries `v` while the changelog headings and the manifest keep the bare form.
5. [P2][Ground truth] The Impact list omitted `src/app/app_entry.h`, where the helper must be declared - resolved (planning review): named together with `app_entry.cpp`.
6. [P2][Ground truth] Task 2.1 demanded an assertion-level red it could not produce, because the test needed a helper that only a later task adds - resolved (planning review): 2.1 now pins `--version` and 2.2 the header line, both reddable against today's binary, and 3.1 adds the token's own cases red-first against the unresolved symbol.
7. [P2][Ground truth] Task 2.1 used `ENCRO_VERSION`/`ENCRO_GIT_COMMIT` in a translation unit that never includes the generated header - resolved (planning review): 2.2 names the `#include "app/version.h"` it needs (`version_tests.cpp` already has it).

## Workflow follow-up

- Commit the planning artifacts as their own `docs:` commit before implementation starts (project convention).
- After implementation: run `openspec-verify-change`, then the code stage of `code-review`, then archive.
