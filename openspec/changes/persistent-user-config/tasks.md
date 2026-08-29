# Tasks: persistent-user-config

## 1. Test isolation groundwork

- [ ] 1.1 Set `ENCRO_CONFIG` to a `TempDir`-scoped path in the unit test fixture (`tests/test_utils.h`) so `commandLineInit` never reads a developer's real config
- [ ] 1.2 Inject `ENCRO_CONFIG` (per-test temp path) into the child process environment in the e2e harness (`tests/e2e/e2e_test_utils.h`) for every invocation unless a test overrides it

## 2. Config store module

- [ ] 2.1 Add `src/cmd/config_store.{h,cpp}`: config path resolution (`ENCRO_CONFIG` → `%LOCALAPPDATA%`/`%APPDATA%` fallback on Windows, `XDG_CONFIG_HOME`/`~/.config` fallback otherwise) with unit tests for the fallback chain (env-var-driven, no real-home reads)
- [ ] 2.2 Implement load: missing file → empty config; malformed JSON → error naming the file and parse issue; JSON values stringified; unknown keys reported with a warning; unit tests for each branch
- [ ] 2.3 Implement save: pretty indented JSON via stream output, canonical key order from the key table, native JSON types (number/boolean/string); unit tests for roundtrip, canonicalization of a compacted file, and parent-directory creation on first write
- [ ] 2.4 Add the `cfg::ConfigKey{...}` token to `option_specs.h`, tag the 16 configurable options in `cmd.cpp` (`color`, `output-format`, `force-conflict-handling`, `jobs`, `ffmpeg-path`, `image-quality`, `crf`, `min-vmaf`, `preset`, `video-codec`, `yes`, `pack`, `keep`, `compress`, `recursive`, `folder-summary`), and collect the per-key validator registry (`std::shared_ptr<Validator>` copies captured at registration, per design D3)

## 3. Precedence merge

- [ ] 3.1 Spike unit test pinning, on CLI11 2.7.2: flag `default_str("true"/"false")` + `force_callback()` behavior, the `--no-x{false}` name-suffix syntax, and that needs/excludes keep evaluating on command-line occurrences only (a config-injected `image-quality` default must not demand `-c`); record fallback decision if anything misbehaves (design D1/D3)
- [ ] 3.2 Load config at the start of `commandLineInit` and inject values as `default_str` plus `force_callback()` on registered options before parse; config load errors surface through `CmdParseResult::error`
- [ ] 3.3 Unit tests: CLI beats config (`--crf 30` over stored 23), config beats built-in default (stored 23 applied), invalid stored value fails the run with a native-style error when effective, invalid stored value unused when CLI overrides the option

## 4. Negation flags and help rendering

- [ ] 4.1 Register dual names on the six persistable flags (`-y,--yes,--no-yes{false}`, `-p,--pack,--no-pack{false}`, `--keep,--no-keep{false}`, `-c,--compress,--no-compress{false}`, `-r,--recursive,--no-recursive{false}`, `-s,--folder-summary,--no-folder-summary{false}`) with unit tests for plain/negation/absent forms
- [ ] 4.2 Add the `--[no-]x` collapse rule to `formatOptionName` and add the `encro config` usage line to the formatter's usage block; update `tests/cmd_help_tiering_tests.cpp` goldens (collapsed renderings, new usage line, no separate `--no-*` entries)
- [ ] 4.3 Unit test: with `ENCRO_CONFIG` pointing at a file containing `{"crf": 23}`, `-h` shows `--crf (=23)`

## 5. Config subcommand

- [ ] 5.1 Register the `config` subcommand in `cmd.cpp` (own help flag, `makeConfigHelpFormatter`, mutually exclusive actions `--list`, `--get <key>`, `--set <key> <value>`, `--unset <key>`, `--path`), populate `CmdParseResult` config fields, set `result.config` via `got_subcommand`, and store the subcommand's own help text in `result.helpText` when matched
- [ ] 5.2 Implement `runConfigCommand` dispatch in `app_entry.cpp` after `handleParseAndHelp` (before `buildAppConfig`/`ensureToolchainReady`); `set` validates values through the registry option's validators (transformers canonicalize), rejecting unknown keys; bare `encro config` prints subcommand help with exit 0
- [ ] 5.3 Unit tests for `list` (value + source columns), `get`, `set` write + validation failures, `unset` fallback to default, `path` output, unknown action failure, and standalone operation with no inputs/toolchain

## 6. E2E coverage

- [ ] 6.1 E2E: `config set crf` then run a fake-ffmpeg encode without `--crf` and assert the persisted value reaches the encode; `--crf` on the CLI still overrides; `config unset` restores default
- [ ] 6.2 E2E: `--no-pack` overrides persisted `pack=true`; `config list/get/path` exit 0 with no inputs; bare `encro config` prints config help exit 0; malformed config file fails the run naming the file, and `config path` still succeeds on the same file

## 7. Wrap-up

- [ ] 7.1 Run `xmake test-parallel` and confirm unit + e2e suites pass, including the untouched suites affected only via help bytes
- [ ] 7.2 Update `-h`/`-hh` help-related docs in `README`/`docs` if they enumerate options or usage lines; run `xmake fmt` and `xmake tidy` and resolve findings
