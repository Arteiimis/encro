## 1. Regression tests (red first)

- [x] 1.1 Add a failing unit test in `tests/cmd_cmd_tests.cpp`: `terminal::configure(Always)` then `parseArgs({"encro", "-h"})` (current code eagerly renders with Always), then `terminal::configure(Never)`, then assert the help string contains no `\x1b[` escape; verify it fails on current code via `xmake test-report` (piped runs auto-suppress, so the Always-before-parse setup is what makes it red)
- [x] 1.2 Add the companion red test: `parseArgs({"encro", "-h"})` first, then `terminal::configure(Always)`, then assert the help string DOES contain ANSI styling despite the piped (non-TTY) test output; verify it also fails red

## 2. Lazy help renderer

- [x] 2.1 Replace `CmdParseResult::helpText` (string) with a `std::function<std::string()>` renderer member (distinct name, e.g. `helpRenderer_`) plus a caching `helpText()` accessor in `src/cmd/cmd.h`; make all eight assignment sites in `buildAndParse` (`src/cmd/cmd.cpp`: post-parse main help, config and completion got-subcommand paths, four `CallForHelp` catch branches, `ParseError` catch) install a lambda capturing its own tree's `CLI::App*` and calling `->help()`; verify `xmake build encro` succeeds
- [x] 2.2 Switch the three production consumers to the accessor — `src/app/app_entry.cpp` help path, `src/cmd/config_command.cpp` bare-config help, `src/cmd/completion_command.cpp` bare-completion help — and update the ~39 `.helpText` test references across `tests/cmd_cmd_tests.cpp`, `tests/cmd_completion_command_tests.cpp`, `tests/cmd_help_tiering_tests.cpp`, `tests/cmd_config_tests.cpp`, and `tests/cmd_organize_tests.cpp` to `helpText()` calls; verify `xmake test-report` compiles and passes with the 1.x tests now green
- [x] 2.3 Restructure the color-invariance test (`tests/cmd_cmd_tests.cpp`, "Help layout is color-mode invariant" section) so each side materializes its string via the accessor while its own color mode is still configured — deferring both reads past `terminal::reset()` would render both under one mode and pass vacuously; verify the test still asserts strip-ANSI equality across color-on/color-off renders

## 3. Effective-mode precedence coverage

- [x] 3.1 Add CLI-level tests: `parseArgs({"encro", "--color", "never", "-h"})` yields `result.color == "never"` and, after `configureFromColorString(result.color)`, ANSI-free help; `--color always -h` under the same non-TTY run yields ANSI-styled help; verify both pass
- [x] 3.2 Add a config-file test using `TempDir` + `ENCRO_CONFIG` override (existing pattern): a config containing `"color": "never"` makes `parseArgs({"encro", "-h"})` and `parseArgs({"encro", "preview", "-h"})` yield `result.color == "never"` and ANSI-free help after configure (injected second-parse paths only; bare `config`/`completion` are spec-excluded probe paths); verify it passes

## 4. Verification

- [x] 4.1 Run `xmake fmt -k` and `xmake tidy`; fix any findings; verify both report clean
- [x] 4.2 Run the full unit suite via `xmake test-report` (no tag filter) and the e2e suite via `xmake build e2e_tests && xmake run e2e_tests`; verify all pass
