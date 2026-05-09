# Domain Pitfalls: CLI Help Coloring & Terminal Color Deepening

**Domain:** CLI tool colored output via semantic color layer
**Researched:** 2026-05-09
**Confidence:** HIGH

## Critical Pitfalls

Mistakes that cause rewrites or major regressions.

### Pitfall 1: formatter_fn Is a Replacement, Not a Decorator

**What goes wrong:** Developer assumes `formatter_fn` wraps/enhances the default formatter's output. Writes a lambda that tries to colorize the default output — but there is no default output available. The lambda receives raw data (App*, string, mode) and must produce the entire help string.

**Why it happens:** CLI11's documentation uses the word "formatter" and "callback" without emphasizing that this is a complete replacement. The name `formatter_fn` sounds like a hook, not a takeover.

**Consequences:** Help output is missing entirely or incomplete. Column alignment broken. Option names and descriptions don't appear. User sees garbled or empty help.

**Prevention:** Before writing `formatter_fn`, understand that you are reimplementing `CLI::Formatter::make_help()`. Read CLI11's default formatter source (`CLI/Formatter.hpp`) to understand what it does. Budget 60-80 lines for the formatter_fn lambda.

**Detection:** Test `encro --help` immediately after setting `formatter_fn`. Compare output against the pre-migration `boost::program_options` help text. Every option group, every option, every description must appear.

### Pitfall 2: ColorsEnabled() Gating Is Per-Stream, Not Global

**What goes wrong:** Developer calls `terminal::colorsEnabled()` without specifying stream, gets the default (`Stream::Stdout`), and uses the result to decide whether to color stderr output.

**Why it happens:** The overload `colorsEnabled(Stream stream = Stream::Stdout)` has a default parameter. Easy to call `colorsEnabled()` when you meant `colorsEnabled(Stream::Stderr)`.

**Consequences:** When stdout is piped to a file but stderr is still a terminal: error messages lose their color (should retain it). Or, when both are piped: no consequence (both return false). The bug is asymmetric — only visible in specific pipe configurations.

**Prevention:** Always explicitly pass the stream: `colorsEnabled(Stream::Stderr)` for error output, `colorsEnabled(Stream::Stdout)` for normal output. The existing `terminal::eprintln()` template already does this correctly — don't bypass it.

**Detection:** Test with `encro --bad-flag 2>&1 | cat` — errors should still have ANSI codes if stderr is a TTY. Test with `encro --bad-flag 2>/dev/null` to ensure colors are suppressed when stderr is redirected.

### Pitfall 3: NO_COLOR Semantics — Present + Non-Empty, Any Value

**What goes wrong:** Developer checks `NO_COLOR=1` specifically, or checks `NO_COLOR=true`, or requires a specific value.

**Why it happens:** Natural to think `NO_COLOR=1` is the expected value. The spec at no-color.org says "when present and not an empty string (regardless of its value)."

**Consequences:** `NO_COLOR=0` (a valid way to say "I want no color") is ignored. `NO_COLOR=yes` is ignored. `NO_COLOR=true` is ignored. Users who follow the spec get color they tried to disable.

**Prevention:** The existing `terminal::noColorRequested()` implementation is correct: it checks `readEnvVar("NO_COLOR").has_value()`. Do not add value checking. Do not add `== "1"` or `== "true"`.

**Detection:** Test with `NO_COLOR=0 encro --help`, `NO_COLOR=yes encro --help`, `NO_COLOR= anything encro --help` — all should produce plain text.

### Pitfall 4: CLI11 Option Group API Differs from boost::program_options

**What goes wrong:** Developer writes `formatter_fn` assuming CLI11's option group iteration works like `boost::program_options::options_description`. Tries `group.options()` or `group.begin()`.

**Why it happens:** Both libraries have "option groups" — but the APIs are different. CLI11 uses `App::get_option_groups()` → `Option_group*` → `get_options()` → `Option*`.

**Consequences:** Compilation errors. Or worse: the formatter_fn compiles but silently skips groups or options because the iteration logic is wrong.

**Prevention:** During implementation, read CLI11's `App.hpp` and `Option_group.hpp` to understand the exact iteration API. Test with a single-group, single-option CLI first before adding all 4 groups and 26 options.

**Detection:** Test `encro --help` and verify all 4 groups appear (General, I/O, Processing, File ops) with all 26 options.

### Pitfall 5: MessageKind Destruction of Backward Compatibility

**What goes wrong:** Removing or renumbering existing `MessageKind` enum values. Adding new values in the middle of the enum. Changing the style mapping for existing values.

**Why it happens:** Enum modification seems harmless. But `MessageKind` is used across the entire codebase — changing `Error`'s style from red to orange, or reordering enum values, can affect every error output path.

**Consequences:** Subtle visual regressions. Error messages that were red become orange (unexpected for users). Enum values used in serialization or comparison could break.

**Prevention:** Only add new values at the END of the `MessageKind` enum. Never remove or renumber existing values. Never change the style mapping of existing values without explicit user-visible rationale. The existing mappings (Error=red, Warning=yellow, Success=green) are industry conventions — don't deviate.

**Detection:** Existing 3033 assertions should catch unexpected output changes. Visual comparison of before/after error output.

## Moderate Pitfalls

### Pitfall 6: Console Width Race Condition

**What goes wrong:** `formatter_fn` is called lazily (when `--help` is processed), but `console_width.h` reads terminal dimensions. If the terminal is resized between CLI initialization and help display, the cached width is stale.

**Why it happens:** `resolveHelpTextLayout()` caches `lineLength` at CLI init time in current code. The formatter_fn is called later.

**Consequences:** Help output wraps at wrong column if user resized terminal after launching the program (rare, but possible in long-running help scenarios or when help is displayed after a delayed error).

**Prevention:** Call `console_width::resolveColumns()` inside `formatter_fn`, not at CLI init time. Width detection is cheap (single `ioctl`/`GetConsoleScreenBufferInfo` call). Alternatively, accept the current behavior — terminal resize during `--help` display is an edge case.

**Detection:** Test by running `encro --help` in a terminal, resizing before output appears. Check if wrapping matches new width.

### Pitfall 7: formatter_fn Is Not Tested by Existing Assertions

**What goes wrong:** The 3033 existing assertions test CLI parsing and business logic, not the visual output of `--help`. The formatter_fn has zero test coverage unless explicitly tested.

**Why it happens:** The existing tests are integration/unit tests focused on parsing correctness. Help output formatting was previously handled by `boost::program_options::print()` which was assumed correct.

**Consequences:** Help rendering bugs (missing options, broken alignment, wrong grouping) go undetected. A regression in the formatter_fn won't be caught by CI.

**Prevention:** Add at least smoke tests for the formatter_fn: capture help output string, assert it contains expected option names ("--input", "--output", "--pack"), group headers ("General options", "Input/Output options"), and the usage line. Test both color-enabled and color-disabled (NO_COLOR=1) paths.

**Detection:** Missing test coverage → CI doesn't catch help rendering bugs.

### Pitfall 8: Incorrect StyleFor() for OptionDesc

**What goes wrong:** Setting `OptionDesc` to a colored style (e.g., `fg(gray)`) instead of plain `{}`.

**Why it happens:** Temptation to "color everything." Developer thinks dimmed descriptions look more polished.

**Consequences:** Description text becomes harder to read (low contrast). Users with visual impairments may find colored body text problematic. Violates the industry convention (cargo, ripgrep, fd all use plain descriptions).

**Prevention:** Map `OptionDesc` to `{}` (no style). Description text is the highest-volume content in help output — readability trumps aesthetics. If users complain about visual distinction, consider increasing spacing between columns, not coloring descriptions.

**Detection:** Visual review of --help output. If descriptions are colored, it's wrong.

### Pitfall 9: Forgetting to Update defaultBadgeLabel()

**What goes wrong:** Adding new `MessageKind` values without adding cases to `defaultBadgeLabel()`.

**Why it happens:** `defaultBadgeLabel()` has a switch with explicit cases for each kind. New values that don't match any case fall through to `return {}` — which is accidentally correct for help kinds (no badge). But it's fragile.

**Consequences:** If a future `MessageKind` value should have a badge but falls through the switch, the badge silently disappears. The inverse (a help kind accidentally getting a badge) would produce `[] OptionGroup header` which looks broken.

**Prevention:** Add explicit cases for ALL new `MessageKind` values in `defaultBadgeLabel()`, returning `{}` for help kinds. Add a default case that asserts/logs on unexpected values in debug builds. Consider `[[nodiscard]]` or compiler warnings.

**Detection:** Test that `terminal::renderMessage(Stream::Stdout, MessageKind::OptionGroup, "Test")` does not prepend a badge. The output should be just the styled text, not `[] Test`.

## Minor Pitfalls

### Pitfall 10: Emoji in Terminal Output

**What goes wrong:** Using emoji as badge replacements (e.g., ❌ for errors, ⚠️ for warnings).

**Why it happens:** Modern terminals support emoji. Some tools use them (e.g., Homebrew uses 🍺). Seems "modern."

**Consequences:** Breaks on older terminals, monospace fonts without emoji support, some Windows console configurations. Emoji width is unpredictable (some are double-width). Inconsistent with the tool's existing `[error]` badge convention.

**Prevention:** Stick to ASCII badges: `[error]`, `[warn]`, `[done]`, `[info]`, `[hint]`, `[?]`. These are universally supported, monospace-safe, and already in use.

### Pitfall 11: Not Testing with TERM=dumb

**What goes wrong:** Developer only tests with a modern terminal emulator (Windows Terminal, iTerm2, GNOME Terminal). Doesn't test with `TERM=dumb` or on a bare TTY.

**Why it happens:** Modern terminals Just Work(TM). It's easy to forget the fallback paths.

**Consequences:** On CI systems (which often set `TERM=dumb` or run without a TTY), color escape sequences appear as garbage characters in logs. `[31m[1m[error][0m file not found` instead of clean text.

**Prevention:** `terminal::colorsEnabled()` already handles `TERM=dumb`. The prevention is testing: run `TERM=dumb encro --help` and verify no ANSI codes appear in output.

**Detection:** CI logs show raw escape sequences instead of clean text.

### Pitfall 12: Windows Console Host (conhost.exe) Before Win10 1903

**What goes wrong:** ANSI escape sequences don't render on older Windows 10 builds or Windows 8.1.

**Why it happens:** Microsoft added Virtual Terminal support in Windows 10 version 1903 (build 18362). Older builds need the legacy Console API for colors.

**Consequences:** Garbled output on older Windows systems. `←[1;34mGeneral options←[0m` instead of colored text.

**Prevention:** `terminal::colorsEnabled()` already calls `enableVirtualTerminal(Stream stream)` which uses `SetConsoleMode(ENABLE_VIRTUAL_TERMINAL_PROCESSING)`. If this fails (old Windows), `colorsEnabled()` returns `false` and all output is plain text. This is the correct behavior — plain text is better than garbage. No code changes needed.

**Detection:** Test on Windows 10 build < 18362 (or simulate by temporarily disabling VT processing in code).

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|-------------|---------------|------------|
| MessageKind enum extension | Adding values in the middle, breaking existing switch cases | Add only at the end. Add explicit cases to all switches (`styleFor`, `defaultBadgeLabel`). |
| formatter_fn implementation | Undocumented CLI11 option group iteration API | Read CLI11 headers (`App.hpp`, `Option_group.hpp`) during implementation. Start with single-group test. |
| Error/warning coloring audit | Missing call sites — some errors still use raw `fmt::print` or `std::cerr` | Grep for `std::cerr`, `fmt::print(stderr`, `std::cout <<.*error` patterns. Audit all 945+ test assertion messages. |
| Version output coloring | Over-coloring — making the entire version block bright | Only the version NUMBER is bright/bold. Build metadata, copyright, license info should be dim/plain. |
| --color flag priority | `--color never` after `NO_COLOR` handling causes confusion | Priority is: `--color always` > `--color never` > `NO_COLOR` > isatty. Documented in no-color.org FAQ. Already correct in `colorsEnabled()`. |
| Progress bar color (deferred) | Mixing terminal:: colors with progress bar library's internal ANSI | Evaluate as separate phase. Do not attempt in v1.6. |

## Sources

- **NO_COLOR Specification:** https://no-color.org/ — HIGH confidence (authoritative). Spec: "present and not an empty string (regardless of its value)." FAQ: command-line args override NO_COLOR; bold/italic/underline NOT affected.
- **CLI11 formatter_fn:** Context7 `/cliutils/cli11` — HIGH confidence (official docs). `formatter_fn` is a complete replacement, not a decorator. AppFormatMode: Normal, All, Sub.
- **CLI11 Rang pitfalls:** Context7 `/cliutils/cli11` (README) — HIGH confidence. The README shows Rang integration with `std::atexit` for reset — explicitly rejected for encro per design notes (rang lacks NO_COLOR support).
- **Windows VT support:** `src/infra/terminal.cpp` lines 82-93 — HIGH confidence (primary source). `enableVirtualTerminal()` uses `SetConsoleMode(ENABLE_VIRTUAL_TERMINAL_PROCESSING)`. Falls back to false on failure → plain text.
- **Existing terminal:: architecture:** `src/infra/terminal.h`, `src/infra/terminal.cpp` — HIGH confidence (primary source). `colorsEnabled()` check order: ColorMode → NO_COLOR → isatty → TERM=dumb → VT enable.
- **rang known issues:** GitHub issue #140 (NO_COLOR support), #133 (style::reset bug) — MEDIUM confidence (training data, not live-fetched). Confirmed by CLI note in `.planning/notes/cli-library-selection.md`.
- **Industry conventions:** cargo, ripgrep, fd, gh — MEDIUM confidence (training data). Patterns consistent across tools: plain descriptions, bold option names, colored section headers.

---

*Pitfalls research for: encrō v1.6 CLI Color Deepening*
*Researched: 2026-05-09*
