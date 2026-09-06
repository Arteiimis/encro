## 1. Registries and tokens

- [ ] 1.1 Add failing registry tests for `recordPath` (long-name keyed) and `recordPositional` (option-pointer keyed) in `tests/cmd_completion_registry_tests.cpp`, then implement both in `src/cmd/completion_registry.{h,cpp}` and verify `xmake test-report --tag="[completion]"` passes
- [ ] 1.2 Add the `cfg::Path{}` token to `src/cmd/option_specs.h` calling `recordPath`, and the positional branch to `cfg::Members` (no lnames/snames ⇒ `recordPositional`), then verify `xmake test-report --tag="[cmd]"` still passes

## 2. Model and emission

- [ ] 2.1 Add failing emitter tests: `pathIds` contains `model_dir` and equals the registry set; completion scope carries positional candidates `bash`/`powershell`; preview/organize positionals carry none. Then extend `ScopeInfo`/`CompletionModel`, build `pathIds` from the registry in `src/cmd/completion_emitter.{h,cpp}`, and annotate the path options in `src/cmd/cmd.cpp` (`--input`, `--inputs`, `--output` main + preview, `--state-file`, `--ffmpeg-path`, `--model-dir`) with `cfg::Path{}`; verify the tests pass and `encro completion bash` lists the five original path ids plus `model_dir`
- [ ] 2.2 Add the positional-slot branch to the bash and PowerShell glue in `src/cmd/completion_emitter.cpp`: skip flags and values of value-taking options when locating the slot (use the existing name-id/value-id tables); enum slot ⇒ prefix-filtered candidates with suppressed fallback; no-candidates slot ⇒ fall through; past-the-end ⇒ nothing; place the branch after the config `--set` branch. Verify `encro completion bash|powershell` emit the new `_ENCRO_POSCANDS_*` / `$__encroPosCands` tables

## 3. End-to-end verification

- [ ] 3.1 Add real-shell TAB probes to `tests/cmd_completion_smoke_tests.cpp` (opt-in): `encro completion <TAB>` ⇒ `bash powershell`; `encro completion p<TAB>` ⇒ `powershell` only; `encro completion --install <TAB>` ⇒ same as bare; `encro completion bash <TAB>` ⇒ nothing; `encro organize --model-dir X <TAB>` still delegates to files; run `ENCRO_TEST_COMPLETION=1 xmake test-report --tag="[smoke]"` to verify
- [ ] 3.2 Run `xmake test-parallel` and confirm no regressions across unit + e2e suites
