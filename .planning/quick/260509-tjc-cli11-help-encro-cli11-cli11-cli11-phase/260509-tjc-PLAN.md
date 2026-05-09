---
phase: quick-260509-tjc
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - src/cmd/cmd.h
  - src/cmd/cmd.cpp
  - src/app/prelude.h
  - src/app/prelude.cpp
  - src/app/app_entry.cpp
autonomous: true
requirements: []

must_haves:
  truths:
    - "encro --help shows the app intro line ('encro: Universal video encoder/converter/packer | build: YYYY-MM-DD HH:MM:SS') as part of CLI11's help output"
    - "No ANSI color codes appear in the intro line (plain text only — Phase 20 adds colors uniformly)"
    - "Option groups and their help text still render identically"
    - "encro without --help behaves identically (no output change)"
  artifacts:
    - path: "src/cmd/cmd.cpp"
      provides: "CLI11 formatter_fn prepends app description to help output"
      contains: "app_ptr->get_description()"
    - path: "src/app/app_entry.cpp"
      provides: "printHelp() outputs only cmd.helpText, no separate terminal::println"
      contains: "cmd.helpText"
  key_links:
    - from: "src/app/app_entry.cpp::helpIntroLine()"
      to: "src/cmd/cmd.cpp::commandLineInit()"
      via: "std::string parameter through prelude::initStartup()"
      pattern: "introLine"
    - from: "CLI11 app.description()"
      to: "formatter_fn lambda body"
      via: "app_ptr->get_description()"
      pattern: "get_description"
---

<objective>
Move the `encro --help` app description line ("encro: Universal video encoder/converter/packer | build: YYYY-MM-DD HH:MM:SS") from a manually-colored `terminal::println(Heading, ...)` call in `app_entry.cpp` into CLI11's help system via `app.description()` → custom `formatter_fn`. Removes premature ANSI color injection (Phase 20 will add colors uniformly).
</objective>

<execution_context>
@C:/Users/LEGION/.config/opencode/get-shit-done/workflows/execute-plan.md
@C:/Users/LEGION/.config/opencode/get-shit-done/templates/summary.md
</execution_context>

<context>
@src/cmd/cmd.h (CmdParseResult, commandLineInit signature)
@src/cmd/cmd.cpp (CLI11 parsing, makeHelpFormatter lambda, formatter_fn helpers)
@src/app/prelude.h (StartupContext, initStartup signature)
@src/app/prelude.cpp (initStartup calls commandLineInit)
@src/app/app_entry.cpp (printHelp uses terminal::println(Heading,...); helpIntroLine() compiles timestamp)

<interfaces>
<!-- Current signature: commandLineInit(int argc, char* argv[]) -> CmdParseResult -->
<!-- Current signature: initStartup(int argc, char* argv[]) -> StartupContext -->
<!-- formatter_fn lambda signature: (CLI::App const* app_ptr, std::string prev, CLI::AppFormatMode mode) -> std::string -->
<!-- helpIntroLine() returns: "encro: Universal video encoder/converter/packer | build: YYYY-MM-DD HH:MM:SS" -->
</interfaces>
</context>

<tasks>

<task type="auto" tdd="false">
  <name>Task 1: Accept intro line in commandLineInit, set app.description(), render in formatter_fn</name>
  <files>src/cmd/cmd.h, src/cmd/cmd.cpp</files>
  <action>
**cmd.h** — Add `std::string const&amp; introLine` parameter to `commandLineInit()`:
```cpp
auto commandLineInit(int argc, char* argv[], std::string const&amp; introLine) -> CmdParseResult;
```

**cmd.cpp** — Three changes:

1. Accept the new parameter in the function definition (line 162):
```cpp
auto commandLineInit(int argc, char* argv[], std::string const&amp; introLine) -> CmdParseResult {
```

2. After `auto app = CLI::App{"Allowed options"};` (line 165), add:
```cpp
app.description(introLine);
```
This stores the intro line as CLI11's app description so the formatter_fn can retrieve it.

3. In the `makeHelpFormatter` lambda (lines 116-158), at the start of the lambda body (after `auto result = std::string{};` on line 121), prepend the app description:
```cpp
auto const desc = app_ptr->get_description();
if (!desc.empty()) {
  result += desc;
  result += "\n\n";
}
```
This renders the intro line as plain text at the top of the help output, before any option groups. The description comes from `app_ptr` (the main CLI::App passed by CLI11 to the formatter), which was set to `introLine` in step 2.
</action>
<verify>
<automated>
# Build and check help output contains the intro line without ANSI codes
pwsh -Command "cd '$PWD' &amp;&amp; xmake build &amp;&amp; ./build/windows/x64/release/encro.exe --help | Select-String 'encro: Universal video encoder/converter/packer \| build:' | ForEach-Object { if ($_.Line -match '\x1b') { throw 'ANSI found' } else { Write-Host 'OK: no ANSI in intro line' } }"
</automated>
</verify>
<done>
- `commandLineInit()` accepts `std::string const&amp; introLine` parameter
- `app.description(introLine)` is set before parsing
- `formatter_fn` lambda reads and outputs `app_ptr->get_description()` as the first line(s) of help
- Help output starts with the intro line as plain text (no ANSI escape sequences)
- All option groups still render identically below the intro line
</done>
</task>

<task type="auto" tdd="false">
  <name>Task 2: Thread intro line from app_entry through prelude; remove colored println</name>
  <files>src/app/prelude.h, src/app/prelude.cpp, src/app/app_entry.cpp</files>
  <action>
**prelude.h** — Add `std::string const&amp; introLine` parameter to `initStartup()`:
```cpp
auto initStartup(int argc, char* argv[], std::string const&amp; introLine) -> StartupContext;
```

**prelude.cpp** — Two changes:

1. Update `initStartup()` definition signature to accept the new parameter (line 135):
```cpp
auto initStartup(int argc, char* argv[], std::string const&amp; introLine) -> StartupContext {
```

2. Forward `introLine` to `commandLineInit()` (line 136):
```cpp
auto cmd = commandLineInit(argc, argv, introLine);
```

**app_entry.cpp** — Three changes:

1. In `run()` (line 163), compute the intro line before calling `initStartup()`, then pass it:
```cpp
auto const introLine = helpIntroLine();
auto const startup = prelude::initStartup(argc, argv, introLine);
```

2. In `printHelp()` (lines 68-72), remove the `terminal::println(Heading, ...)` call and the extra newline — keep only the `cmd.helpText` output:
```cpp
auto printHelp(CmdParseResult const&amp; cmd) -> void {
  std::cout &lt;&lt; cmd.helpText;
}
```
The intro line is now part of `cmd.helpText` (rendered by CLI11's formatter_fn). No separate `terminal::println` call needed.

3. Keep `#include "infra/terminal.h"` and `using enum terminal::MessageKind;` — they are still used by `failWithHint()` for `Error` and `Hint` enum values. Only `Heading` usage is removed.
</action>
<verify>
<automated>
# Build and verify: help output is correct, no ANSI codes anywhere in intro
pwsh -Command "cd '$PWD' &amp;&amp; xmake build &amp;&amp; $out = &amp; ./build/windows/x64/release/encro.exe --help; if ($out[0] -notmatch 'encro: Universal') { throw 'Missing intro line' }; if ($out[0] -match '\x1b') { throw 'ANSI in intro' }; Write-Host 'PASS: intro line is plain text, part of CLI11 output'"

# Verify non-help mode unaffected (no output to stdout on success)
pwsh -Command "cd '$PWD' &amp;&amp; xmake build &amp;&amp; $out = &amp; ./build/windows/x64/release/encro.exe --help 2&gt;$null; if ($LASTEXITCODE -ne 0) { throw 'encro --help exit code non-zero' }"
</automated>
</verify>
<done>
- `prelude::initStartup()` accepts and forwards `introLine` to `commandLineInit()`
- `app_entry::run()` passes `helpIntroLine()` result through `initStartup()`
- `printHelp()` outputs only `cmd.helpText` — no `terminal::println(Heading, ...)` call
- `encro --help` shows intro line at top, then option groups, all plain text
- `encro` (no flags) exits with error as before, unaffected
</done>
</task>

</tasks>

<verification>
## Build and smoke test

```powershell
# Build
xmake build

# Help output must start with intro line (no ANSI codes)
$help = & ./build/windows/x64/release/encro.exe --help
$help[0] -match 'encro: Universal video encoder/converter/packer \| build: \d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}'
$help[0] -notmatch '\x1b'   # No ANSI escape sequences in intro

# Check that option groups still appear
$help -match 'General options'
$help -match 'Input/Output options'
$help -match 'Processing options'
$help -match 'File operation options'

# Check key options still listed
$help -match '--help'
$help -match '--input'
```
</verification>

<success_criteria>
- `encro --help` output starts with the plain-text intro line: `encro: Universal video encoder/converter/packer | build: YYYY-MM-DD HH:MM:SS`
- No ANSI escape sequences (`\x1b[...m`) appear in the intro line
- All four option groups and their options render identically to Phase 19 output
- `encro` without `--help` produces no stdout output (unaffected)
- Build compiles with zero warnings
</success_criteria>

<output>
After completion, create `.planning/quick/260509-tjc-cli11-help-encro-cli11-cli11-cli11-phase/260509-tjc-SUMMARY.md`
</output>
