## 1. The table type and the assembler (tests first)

- [x] 1.1 Add the grown `KeyDef` (key, kind, long name, built-in default, validators) and `KeyTable::find` to `src/cmd/config_store.h`; verify `xmake build encro` compiles with `config_store.cpp` still on the old `keys()`
- [x] 1.2 Add `tests/config_store_tests.cpp` cases (tag `[cmd][config-store]`) for `assembleKeyTable`: a token with no order entry names the key, an order entry with no token names it, a duplicate key fails, and a clean input yields canonical order — verify they fail before the implementation exists (red first)
- [x] 1.3 Implement `assembleKeyTable` (pure: ordering + rejections) and add the canonical-order name list (`std::to_array<std::string_view>({…})`) beside the still-used `kKeys` in `src/cmd/config_store.cpp`; verify all four cases pass and `xmake test-report --tag="[config-store]"` is green

## 2. Derive the kind at registration

- [x] 2.1 Add the constexpr `cfg::ConfigKey` finder over a spec's `cfg` tuple and the `jsonKindFor<Ty>()` mapping (design D2) in `src/cmd/option_specs.h`; write the case first — it fails until the mapping lands — and verify one key of each class (`yes` → Boolean, `crf` → Number, `color` → String)
- [x] 2.2 Make `registerOne` record a `KeyDef` (key, derived kind, long name, `get_default_str()`, validator list copied after the spec's `cfg` tuple has been applied) into a `std::vector<KeyDef>&` on the parse result (already in every registrar's signature); verify the 2.1 case also asserts one entry per token-carrying spec and none for a token-less one such as `--dry-run`

## 3. Table ownership and the config command

- [x] 3.1 Give `CmdParseResult` the assembled `KeyTable`; assemble in `buildAppTree` after the last registration and report a mismatch through `result.error`; verify `xmake run encro config list` still prints the 17 keys in canonical order
- [x] 3.2 Move `keys()`/`isKnownKey`/`jsonKindOf`/`validateValue` onto the table (`table.find`, `table.validate`) and thread it to every reader (`config list`, `save()`, `load`, `injectConfigDefaults`); delete the literal `kKeys` array plus `configKeyRegistry()`/`captureConfigKey`; verify `rg -n "kKeys|configKeyRegistry|captureConfigKey" src` returns nothing and `xmake test-report --tag="[config-store]"` is green
- [x] 3.3 Point `config_command.cpp`'s `builtinDefault` at the captured default string; verify `xmake run encro config list` shows the same defaults/sources and the `[config]` cases in `tests/cmd_config_tests.cpp` pass
- [x] 3.4 Re-point `tests/config_store_tests.cpp:199` (validator order + canonicalization) at `assembleKeyTable` + `KeyTable::validate`, keeping its named failure mode, and assert canonicalization through the copied list both synthetically (`"low"` → `"0"`) and on the real registered `color` entry (`"ALWAYS"` → `"always"`); drop its `no-such-key` assertion — unknown keys never reach validation (`config_command.cpp:65,76,100`)
- [x] 3.5 Delete the `keys().size() == 17` guard (`tests/config_store_tests.cpp:142`) and verify the remaining config-store cases and the canonical-order case (`:170`) pass

## 4. Completion reads the same table

- [x] 4.1 Drop `completion::recordConfigKey` from `cfg::ConfigKey`'s functor and record key → long name from the assembled table in `buildAppTree` instead; verify `tests/cmd_completion_registry_tests.cpp` and `tests/cmd_completion_capture_tests.cpp` pass unchanged
- [x] 4.2 Verify the emitter still sees every key: `xmake run encro completion bash | rg "_ENCRO_CONFIG_KEYS"` lists all 17 keys, and the generated candidate values for `crf`/`preset` are unchanged

## 5. Verification & commits

- [x] 5.1 Run `xmake test-report` (full unit suite) and confirm zero failures
- [x] 5.2 Run `xmake test-parallel` and confirm every shard reports its assigned case count
- [x] 5.3 Run `xmake fmt` before committing; verify a second run produces no further changes
- [x] 5.4 Commit the planning artifacts first as their own `docs:` commit (proposal, design, tasks, `.openspec.yaml`), then implementation + tests + ticked `tasks.md` in one `refactor:` commit (English, conventional, subject < 72 chars, body wrapped at 80)
