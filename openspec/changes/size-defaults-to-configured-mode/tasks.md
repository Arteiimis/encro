# Tasks

## 1. `xmake size` follows the configured build

- [ ] 1.1 Default `--mode` to the configured mode — drop the `"release"` default from the menu entry, resolve `option.get("mode") or config.get("mode") or "release"` in `on_run`, and say so in the option's help text — and verify in a `debug`-configured tree that `xmake size` fails naming `<abs>\build\windows\x64\debug\encro.exe` (the old default named `release`), that `xmake size --mode=release` still prints the release section overview, and that `xmake size -d` prints the breakdown once `encro` is built in debug.
- [ ] 1.2 Take the path's architecture from `config.arch() or os.arch()` instead of `os.arch()`, with a comment that names why the architecture comes from the configuration and that for a binary target the join matches `targetfile()` only while no target overrides its output directory or file name, and verify against a non-host architecture by overriding it in memory — `xmake lua -c "import('core.base.task'); import('core.project.config'); config.load(); config.set('arch','arm64'); print(config.arch(), os.arch()); task.run('size')"` reports `binary not found: <abs>\build\windows\arm64\release\encro.exe` while `os.arch()` stays `x64` (the old code would have named `x64`). Configuring the tree for `arm64` is not possible in this repository: `xmake f -m release -a arm64 -y` fails while resolving the vendored `onnxruntime-gpu` package, which is x64-scoped.

## 2. Records and acceptance

- [ ] 2.1 Flip the `xmake size --mode` entry in `docs/backlog.md` to `RESOLVED:` with the observed behaviour and the change name, and verify the entry states what `xmake size` now analyses and that nothing in the repository calls the task.
- [ ] 2.2 Restore the release configuration and run the task as a user would — `xmake size` prints the section overview, `xmake size -d` prints the per-object breakdown, `xmake size --help` names the configured-mode default — and verify the project configuration is untouched afterwards (`xmake lua -c "import('core.project.config'); config.load(); print(config.get('mode'), config.arch())"` still reports `release x64`).

## Workflow follow-up

- Run `openspec-verify-change` for this change, then the project's code review, before committing.
- Archive the change once the review chain is satisfied.
