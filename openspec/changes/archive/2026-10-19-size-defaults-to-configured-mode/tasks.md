# Tasks

## 1. `xmake size` follows the configured build

- [x] 1.1 Default `--mode` to the configured mode — drop the `"release"` default from the menu entry, resolve `option.get("mode") or config.get("mode") or "release"` in `on_run`, and say so in the option's help text — and verify in a `debug`-configured tree that `xmake size` fails naming `<abs>\build\windows\x64\debug\encro.exe` (the old default named `release`), that `xmake size --mode=release` still prints the release section overview, and that `xmake size -d` prints the breakdown once `encro` is built in debug.
- [x] 1.2 Take the path's architecture from `config.arch() or os.arch()` instead of `os.arch()`, with a comment that names why the architecture comes from the configuration and that for a binary target the join matches `targetfile()` only while no target overrides its output directory or file name, and verify against a non-host architecture by overriding it in memory — `xmake lua -c "import('core.base.task'); import('core.project.config'); config.load(); config.set('arch','arm64'); print(config.arch(), os.arch()); task.run('size')"` reports `binary not found: <abs>\build\windows\arm64\release\encro.exe` while `os.arch()` stays `x64` (the old code would have named `x64`). Configuring the tree for `arm64` is not possible in this repository: `xmake f -m release -a arm64 -y` fails while resolving the vendored `onnxruntime-gpu` package, which is x64-scoped.

## 2. Records and acceptance

- [x] 2.1 Flip the `xmake size --mode` entry in `docs/backlog.md` to `RESOLVED:` with the observed behaviour and the change name, and verify the entry states what `xmake size` now analyses and that nothing in the repository calls the task.
- [x] 2.2 Restore the release configuration and run the task as a user would — `xmake size` prints the section overview, `xmake size -d` prints the per-object breakdown, `xmake size --help` names the configured-mode default — and verify the project configuration is untouched afterwards (`xmake lua -c "import('core.project.config'); config.load(); print(config.get('mode'), config.arch())"` still reports `release x64`).

## Workflow follow-up

- Run `openspec-verify-change` for this change, then the project's code review, before committing.
- Archive the change once the review chain is satisfied.

## Review findings

Code-diff stage only: the change declares `skip_specs`, so no verify pass ran and there are no delta specs to review. Tier **S** from the classifier (`opencode/jev-1.13`: tier S 0.98, confidence 0.97, mechanical 0.04), so one reviewer held Standards, Spec and Leanness over the working-tree diff at fixed point `a09ec521` (2 files, 22 insertions / 17 deletions).

- F1 [Standards] `resolved` — the backlog entry's Root-cause prose cited `plugins/size/xmake.lua:26` and `:9`, stale as soon as the change landed (`:26` is now the `ext` local, `:9` no longer holds the old menu literal). The historical prose now quotes the old default as "then `{…}`" with no line number, and the join sentence carries none either; the live pointer to `plugins/coverage/xmake.lua:29` stays.
- F2 [Standards] `resolved` — provenance was recorded twice and in the wrong label: Found named no change while Status carried "recorded by". Found now names `simplify-xmake-build-scripts` and Status names only the fixing change, matching the sibling entries.
- Spec and Leanness: `none`. Rejected findings: none.
- Fix verification (fresh read-only reviewer, round 1): both resolved, no regressions.
