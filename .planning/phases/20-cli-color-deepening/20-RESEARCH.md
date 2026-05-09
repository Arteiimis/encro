# Phase 20: CLI Color Deepening - Research

**Researched:** 2026-05-09
**Domain:** CLI terminal coloring using fmt + existing terminal infrastructure
**Confidence:** HIGH

## Summary

Phase 20 injects semantic ANSI terminal coloring into all CLI output paths (--help, --version, errors) by extending the existing `terminal::MessageKind` enum and wrapping plain-text formatter_fn output with `terminal::styledText()`. The underlying infrastructure — fmt library coloring, NO_COLOR compliance gating via `colorsEnabled()`, and the CLI11 formatter_fn hook — is already fully built and verified from Phase 19. The phase is purely additive: extend the enum, extend `styleFor()`/`defaultBadgeLabel()`, wrap existing formatter_fn text with color, and add a `--version` flag.

The critical technical constraint is **ANSI padding order**: plain-text column widths MUST be computed before color injection. If ANSI escape codes are embedded during padding calculation, column alignment breaks because escape sequences are invisible characters that still count toward string length. Phase 19's formatter_fn already computes max option name width on plain text; Phase 20 must apply `styledText()` AFTER padding, not before.

**Primary recommendation:** Extend MessageKind enum with 5 new values at the end, add their styleFor() mappings, then wrap each formatter_fn text element with `terminal::styledText(Stream::Stdout, kind, text)` AFTER computing column widths. Add --version flag to General group. Zero changes to NO_COLOR gating — already correct.

## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01:** Color scheme — three-layer: Usage/heading descriptions in `steel_blue`+bold (`MessageKind::Usage`), option group titles in `steel_blue`+bold (`MessageKind::OptionGroup`), option names in `light_cyan` (`MessageKind::OptionName`), option descriptions plain (`MessageKind::OptionDesc`). Default value annotations `(=mp4)` follow OptionDesc — no separate coloring.
- **D-02:** ANSI padding before color injection — compute plain-text column widths first, pad to alignment, THEN apply `terminal::styledText()`. Padding after coloring misaligns columns due to invisible escape codes.
- **D-03:** `--version` flag added to General group after `--help`. Position: same as helpFlag in `commandLineInit()`.
- **D-04:** Version output format: `encro v1.6 (build: YYYY-MM-DD HH:MM:SS)` — milestone version v1.6 (not build version 0.1.5). Compile timestamp reused from existing `compileTimestamp()`.
- **D-05:** Version colored via `terminal::println(Version, ...)` — new `MessageKind::Version`. No badge prefix for Version.
- **D-06:** Error messages already unified — all error paths already use `failWithHint()` → `terminal::println(Error, ...)`. Parser errors (CLI11 native), config validation, toolchain, pipelines — all consistent. No additional error coloring work needed.
- **D-07:** `MessageKind` enum extended with 5 values appended at end: `Usage`, `OptionGroup`, `OptionName`, `OptionDesc`, `Version`. Additive-only, no renumbering, backward compatible.
- **D-08:** Each new MessageKind needs `styleFor()` mapping and `defaultBadgeLabel()` case. `Version` uses empty badge label (no prefix).
- **D-09:** All new color paths gated through `terminal::styledText()` with explicit `Stream::Stdout` — internal `colorsEnabled(stream)` call handles NO_COLOR, piped output, and `--color never`.
- **D-10:** No new environment variable detection needed — `terminal::configureFromColorString()` already handles `--color auto|always|never`. `NO_COLOR` env var detected by `terminal::noColorRequested()` in `colorsEnabled()`.

### the agent's Discretion

- Exact implementation order of the 5 MessageKind values
- Internal helper function structure within formatter_fn
- Whether to modify existing `formatOptionHelp`/`formatGroupHeader` helpers or add new wrapper functions
- Test case structure and coverage scope for colored output smoke tests
- `--version` flag name and short option (--version only, no short form per D-03)

### Deferred Ideas (OUT OF SCOPE)

- Progress bar coloring — indicators library has own color system, deferred to v2+
- CLI option conflict error message enhancement (separate quick task or future phase)

## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| COLR-01 | --help output colored by section (Usage/OptionGroup/OptionName/OptionDesc) via formatter_fn | formatter_fn structure exists from Phase 19; `terminal::styledText()` returns colored string; insert wrappers around formatOptionHelp/formatGroupHeader return values after padding |
| COLR-02 | MessageKind enum extended with 5 values + styleFor() mappings | enum currently 8 values (Plain..Heading); 5 added at end (additive-only per conventions); styleFor() switch extended with 5 cases; defaultBadgeLabel() extended; fmt::color values VERIFIED |
| COLR-03 | Error messages unified via terminal::println(Error) / eprintln() | D-06 confirms all error paths already use failWithHint() → println(Error). NO additional work needed. |
| COLR-04 | --version output colored (new flag + MessageKind::Version) | New CLI11 flag in General group; compileTimestamp() reused; println(Version, fmt, args) for output; CmdParseResult.version bool field |
| COLR-05 | NO_COLOR standard compliance on all new color paths | colorsEnabled() already gates all color; noColorRequested() checks NO_COLOR env var; styledText() returns plain text when colors disabled; zero changes needed |

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Help text colored rendering | CLI/Entry Layer (cmd.cpp) | Infrastructure (terminal.h) | formatter_fn in cmd.cpp assembles help string; calls into terminal::styledText() for color injection |
| MessageKind enum + styleFor() mapping | Infrastructure (terminal.h/.cpp) | — | Central styling registry; all consumers read from it; additive-only extension |
| NO_COLOR compliance gating | Infrastructure (terminal.cpp) | — | colorsEnabled() already per-stream; noColorRequested() checks NO_COLOR; zero changes needed |
| --version flag parsing | CLI/Entry Layer (cmd.cpp) | — | New CLI11 add_flag() call in commandLineInit(); CmdParseResult.version bool |
| --version output | CLI/Entry Layer (app_entry.cpp) | Infrastructure (terminal.h) | handleParseAndHelp() checks cmd.version; outputs via terminal::println(Version, ...) |
| Error coloring consistency | Infrastructure (terminal.cpp) | CLI/Entry Layer (app_entry.cpp) | Already unified via failWithHint() → println(Error) in Phase 19; zero changes per D-06 |
| ANSI escape code injection | Infrastructure (terminal.cpp) | — | styledText() adds ANSI via fmt::format(styleFor(kind), ...); gated by colorsEnabled() |
| Column width calculation | CLI/Entry Layer (cmd.cpp) | — | Plain-text maxColLen computed before color injection per D-02; no ANSI in padding stage |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| fmt | ~11.x (via xmake `add_requires("fmt")`) | ANSI terminal color output via `fmt::color` enum + `fmt::text_style` | Already project dependency; provides `fg()`, `emphasis::bold`, CSS named colors; used by existing terminal module |
| CLI11 | v2.6.2 (via xmake `add_requires("cli11")`) | CLI parsing + formatter_fn hook | Migrated in Phase 19; formatter_fn lambda already renders entire help string |
| terminal module | N/A (project code) | Central color/style registry; `styledText()`, `styleFor()`, `colorsEnabled()` | Already built; extend MessageKind enum + styleFor() entries |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Catch2 | v3.x | Test framework for smoke tests | New colored output verification tests |
| spdlog | ~1.x | Logging (errors also log to file) | Already consumed by failWithHint(); no changes needed |

### Alternatives Considered
None — all dependencies already in project. Phase is pure extension of existing infrastructure.

**Installation:** No new packages needed. All dependencies already in `xmake.lua`.

## Architecture Patterns

### System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                    CLI Entry (app_entry.cpp)                      │
│  handleParseAndHelp():                                           │
│    if cmd.version → terminal::println(Version, "encro v1.6...")  │
│    if cmd.help    → std::cout << cmd.helpText                     │
│    if cmd.error   → failWithHint() → terminal::println(Error)    │
└──────────────────────────┬──────────────────────────────────────┘
                           │ cmd.helpText (string, already colored)
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│                    CMD Layer (cmd.cpp)                            │
│  commandLineInit():                                              │
│    register --version flag (CLI11 add_flag)                      │
│    app.formatter_fn(makeHelpFormatter(...))                      │
│                                                                  │
│  makeHelpFormatter() → lambda (formatter_fn):                    │
│    1. Compute plain-text maxColLen (names + types + defaults)    │
│    2. For each group:                                            │
│       a. formatGroupHeader(name) → wrap in styledText(OptionGroup)│
│       b. For each option:                                        │
│          - Build plain firstCol (name + type + default)          │
│          - Pad to colWidth (plain text, no ANSI)                 │
│          - Wrap name in styledText(OptionName, nameStr)          │
│          - Wrap description in styledText(OptionDesc, desc)      │
│    3. Return assembled string                                    │
└──────────────────────────┬──────────────────────────────────────┘
                           │ calls styledText(stream, kind, text)
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│              Infrastructure (terminal.h/.cpp)                     │
│                                                                  │
│  MessageKind enum (13 values after extension):                   │
│    Plain, Error, Warning, Success, Info, Hint, Prompt, Heading,  │
│    Usage, OptionGroup, OptionName, OptionDesc, Version  ← NEW    │
│                                                                  │
│  styleFor(kind) → fmt::text_style:                               │
│    OptionGroup → fg(steel_blue) | bold                           │
│    OptionName  → fg(light_cyan)                                  │
│    OptionDesc  → {} (plain)                                      │
│    Usage       → fg(steel_blue) | bold                           │
│    Version     → fg(steel_blue) | bold                           │
│                                                                  │
│  styledText(stream, kind, text) → string:                        │
│    if !colorsEnabled(stream): return text                        │
│    return fmt::format(styleFor(kind), "{}", text)                │
│                                                                  │
│  colorsEnabled(stream) → bool:                                   │
│    ColorMode::Never → false                                      │
│    ColorMode::Always → true                                      │
│    ColorMode::Auto → check NO_COLOR, isatty, TERM, VT support    │
└─────────────────────────────────────────────────────────────────┘
```

### Recommended Project Structure
```
src/
├── infra/
│   ├── terminal.h          # EXTEND: MessageKind enum (add 5 values at end)
│   └── terminal.cpp        # EXTEND: styleFor() switch, defaultBadgeLabel() switch
├── cmd/
│   ├── cmd.h               # EXTEND: CmdParseResult.version bool
│   └── cmd.cpp             # MODIFY: add --version flag, wrap formatter_fn with styledText()
└── app/
    └── app_entry.cpp       # MODIFY: handleParseAndHelp() check cmd.version
```

### Pattern 1: Additive Enum Extension
**What:** Append new MessageKind values at the end of the enum, never reorder or renumber.
**When to use:** Always for MessageKind — Phase 19 research established this as the only safe pattern.
**Example:**
```cpp
// Source: src/infra/terminal.h (existing) + CONTEXT.md D-07
enum class MessageKind {
  Plain,      // 0
  Error,      // 1
  Warning,    // 2
  Success,    // 3
  Info,       // 4
  Hint,       // 5
  Prompt,     // 6
  Heading,    // 7
  // ── Phase 20 additions ──
  Usage,      // 8
  OptionGroup,// 9
  OptionName, // 10
  OptionDesc, // 11
  Version,    // 12
};
```

### Pattern 2: styleFor() Case Extension
**What:** Add a switch case per new MessageKind value mapping to fmt::text_style.
**When to use:** Every new MessageKind value needs a styleFor() entry.
**Example:**
```cpp
// Source: src/infra/terminal.cpp (styleFor() function, line 186-202)
// ADD after the Heading case (line 198):
case MessageKind::Usage      : return fg(fmt::color::steel_blue) | emphasis::bold;
case MessageKind::OptionGroup: return fg(fmt::color::steel_blue) | emphasis::bold;
case MessageKind::OptionName : return fg(fmt::color::light_cyan);
case MessageKind::OptionDesc : return {};
case MessageKind::Version    : return fg(fmt::color::steel_blue) | emphasis::bold;
```

### Pattern 3: defaultBadgeLabel() Case Extension
**What:** Return appropriate badge label string for each MessageKind.
**When to use:** Every new MessageKind needs a case.
**Example:**
```cpp
// Source: src/infra/terminal.cpp (defaultBadgeLabel() function, line 96-109)
// ADD before the closing return:
case MessageKind::Usage      : return {};
case MessageKind::OptionGroup: return {};
case MessageKind::OptionName : return {};
case MessageKind::OptionDesc : return {};
case MessageKind::Version    : return {};
```
All 5 new kinds return empty badge labels (no prefix) — help/version output should not have `[badge]` prefixes.

### Pattern 4: ANSI-Padding-Neutral Color Injection
**What:** Compute column alignment on plain text, apply color AFTER padding.
**When to use:** Whenever colored text participates in column alignment.
**Example:**
```cpp
// Source: CONTEXT.md D-02, cmd.cpp formatter_fn (Phase 19 pattern)
// WRONG (padding after color — escape codes break alignment):
// auto colored = terminal::styledText(Stdout, OptionName, firstCol);
// auto padded = std::format("  {}{:<{}}{}\n", colored, "", gap, desc);
//
// CORRECT (padding before color — plain text first):
auto const plain = nameStr + typeStr + defaultStr;
auto const padded = std::format("  {}{:<{}}", plain, "", gap);
// THEN apply color to each semantic element
auto const coloredName = terminal::styledText(Stdout, OptionName, nameStr);
auto const coloredDesc = terminal::styledText(Stdout, OptionDesc, description);
result += std::format("  {}{}{:<{}}{}\n", coloredName, typeStr, defaultStr, gap, coloredDesc);
```

### Pattern 5: --version Flag Pattern
**What:** Register flag in General group, check in handleParseAndHelp(), print and exit 0.
**Example:**
```cpp
// In cmd.cpp commandLineInit():
auto* versionFlag = app.add_flag("--version", "show version information");

// In CmdParseResult: bool version = false;
// After parse: result.version = versionFlag->count() > 0;

// In app_entry.cpp handleParseAndHelp():
if (cmd.version) {
  terminal::println(
    Version,
    "encro v1.6 (build: {})",
    compileTimestamp()
  );
  return 0;
}
```

### Anti-Patterns to Avoid
- **Coloring before padding:** ANSI escape codes have display width 0 but string length >0. Computing padding on a string containing escape codes produces misaligned columns. Always compute widths on plain text, THEN inject color.
- **Adding MessageKind values mid-enum:** Would shift existing enum values and break binary compatibility. Always append at end.
- **Bypassing styledText():** Using raw `fmt::format(styleFor(kind), ...)` directly instead of `terminal::styledText()` skips the `colorsEnabled()` gate. All color paths MUST go through `styledText()`.
- **Omitting Stream parameter:** `styledText()` defaults to `Stream::Stdout` but explicit parameter is safer per Pitfall #2 (asymmetric coloring in piped scenarios). Always pass `Stream::Stdout` explicitly for help/version.
- **Badge prefix on help elements:** `defaultBadgeLabel()` must return empty strings for Usage, OptionGroup, OptionName, OptionDesc, Version. Help output should never show `[usage]` or `[optiongroup]` prefixes.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| ANSI escape code generation | Raw `\x1b[...m` strings | `fmt::format(text_style, ...)` via `terminal::styledText()` | fmt handles cross-platform VT processing, Windows console mode, escape sequence correctness |
| Color enable/disable logic | Custom env var checks | `terminal::colorsEnabled(stream)` | Already implements NO_COLOR, --color, isatty, TERM=dumb, Windows VT detection |
| NO_COLOR env var detection | Custom `getenv("NO_COLOR")` | `terminal::noColorRequested()` → `colorsEnabled()` | Already called inside colorsEnabled(); zero additional code needed |
| Help text layout engine | Custom text formatting | CLI11 formatter_fn (existing) | Already renders help string; color injection is a wrapping concern, not a layout concern |
| Option name/description extraction | Custom parsing of CLI11 options | `opt->get_lnames()`, `opt->get_snames()`, `opt->get_description()` | CLI11 API already used in Phase 19 formatter_fn; zero changes to extraction logic |
| Column width calculation | Custom width solver | Existing `maxColLen` loop in formatter_fn | Already computes plain-text max width across all options; perfect for D-02 padding-before-color |

**Key insight:** This phase is a color *wrapping* phase, not a layout phase. The entire help text layout engine (formatter_fn, formatOptionHelp, formatGroupHeader, column width calculation) is already working from Phase 19. The only change is wrapping the rendered text elements with `terminal::styledText()` after padding is applied. Do not rebuild any layout logic.

## Common Pitfalls

### Pitfall 1: ANSI Escape Codes Break Column Alignment
**What goes wrong:** Computing `maxColLen` on strings that already contain ANSI escape codes. An option name like `\x1b[38;2;224;255;255m--help\x1b[0m` has string length 43 but display width 6. The padding calculation produces wildly wrong results.
**Why it happens:** `std::string::size()` counts escape code characters. Terminal rendering ignores them.
**How to avoid:** Compute `maxColLen` on PLAIN text (existing Phase 19 code already does this). Apply `styledText()` only when assembling the final string — after padding is computed and applied.
**Warning signs:** Help output columns don't line up. Option descriptions start at different columns for different options.

### Pitfall 2: Missing `#include "infra/terminal.h"` in cmd.cpp
**What goes wrong:** cmd.cpp currently doesn't include terminal.h (it generates plain text only). Adding color requires the include.
**Why it happens:** Phase 19 explicitly excluded color — cmd.cpp had no reason to depend on terminal.
**How to avoid:** Add `#include "infra/terminal.h"` to cmd.cpp includes. Also add `using enum terminal::MessageKind;` for convenient enum value access.
**Warning signs:** Compiler errors: `terminal::styledText not found`, `MessageKind::OptionName not declared`.

### Pitfall 3: formatter_fn Zero Test Coverage (STATE.md Pitfall #7)
**What goes wrong:** Phase 19's formatter_fn has zero assertions that verify the help string content. If color injection accidentally drops an option name or mangles a description, no existing test catches it.
**Why it happens:** Phase 19 tests focused on parsing correctness, not help output content (beyond line width checks).
**How to avoid:** Add smoke tests that capture the colored help string and assert:
- Contains expected option names (`--help`, `--input`, `--verbose`)
- Contains expected group headers (`General:`, `IO:`, `Processing:`, `FileOp:`)
- Does NOT contain raw ANSI when NO_COLOR is set
- Help text is non-empty and contains "Allowed options" or intro line
**Warning signs:** Help output tests pass but actual terminal output is garbled or missing sections.

### Pitfall 4: colorsEnabled() Without Explicit Stream Parameter
**What goes wrong:** `styledText()` defaults to `Stream::Stdout`, but if called from a context where stderr is the intended output, color gating may be inconsistent.
**Why it happens:** Pitfall #2 from STATE.md — `colorsEnabled()` is per-stream. A pipe on stdout might disable color there but stderr still supports it, or vice versa.
**How to avoid:** Always pass `Stream::Stdout` explicitly for help/version output (`terminal::styledText(terminal::Stream::Stdout, kind, text)`). Error paths use `println(Error, ...)` which hardcodes Stdout.
**Warning signs:** Colored output appearing in piped contexts where it shouldn't, or color missing in terminal contexts where it should appear.

### Pitfall 5: --version Flag Conflict with CLI11's set_help_flag("")
**What goes wrong:** CLI11's `app.set_help_flag("")` disables the default help flag. The new --version flag must be an ordinary flag, not a special CLI11 flag. Treating it as a help-like flag could cause early exit before parsing completes.
**Why it happens:** CLI11's help flag throws `CLI::Success` to exit early. Version should NOT do this — it should be caught by the normal parse flow and handled in `handleParseAndHelp()`.
**How to avoid:** Register --version as a normal `app.add_flag("--version", "show version information")`, not as a help flag. Check `result.version` in `handleParseAndHelp()` along with `result.help`.
**Warning signs:** `encro --version` exits before parsing completes or throws an unhandled exception.

## Code Examples

Verified patterns from official sources:

### fmt::color Verification (ALL CONFIRMED in fmt/color.h source)
```cpp
// Source: https://github.com/fmtlib/fmt/blob/master/include/fmt/color.h
// VERIFIED: All color constants exist in the enum class color
fmt::color::steel_blue     // 0x4682B4 — used for Usage, OptionGroup, Version
fmt::color::light_cyan     // 0xE0FFFF — used for OptionName
fmt::color::light_sky_blue // 0x87CEFA — used for path() styling (existing)
fmt::color::floral_white   // 0xFFFAF0 — used for value() styling (existing)
fmt::color::slate_gray     // 0x708090 — used for Hint message (existing)
fmt::color::golden_rod     // 0xDAA520 — used for count() styling (existing)
fmt::emphasis::bold        // 1 — bit flag, combinable with |

// Usage pattern (verified in fmt docs + existing terminal.cpp):
fg(fmt::color::steel_blue) | fmt::emphasis::bold  // steel_blue + bold
fg(fmt::color::light_cyan)                         // light_cyan, normal weight
```

### Style Mapping (to be added to terminal.cpp)
```cpp
// Source: terminal.cpp styleFor() function, extending existing pattern
// VERIFIED colors against fmt/color.h enum
case MessageKind::Usage      : return fg(fmt::color::steel_blue) | emphasis::bold;
case MessageKind::OptionGroup: return fg(fmt::color::steel_blue) | emphasis::bold;
case MessageKind::OptionName : return fg(fmt::color::light_cyan);
case MessageKind::OptionDesc : return {};
case MessageKind::Version    : return fg(fmt::color::steel_blue) | emphasis::bold;
```

### CLI11 formatter_fn Signature (existing pattern)
```cpp
// Source: src/cmd/cmd.cpp (Phase 19), CLI11 docs
// Signature: std::string(const CLI::App*, std::string, CLI::AppFormatMode)
app.formatter_fn(makeHelpFormatter(general, io, processing, fileop));
```

### NO_COLOR Standard (verified at no-color.org)
```
Rule: "Command-line software which adds ANSI color to its output by default
       should check for a NO_COLOR environment variable that, when present
       and not an empty string (regardless of its value), prevents the
       addition of ANSI color."

Implementation in terminal.cpp (already exists, zero changes needed):
- noColorRequested() checks getenv("NO_COLOR") — any non-empty value → true
- colorsEnabled() checks ColorMode, then calls noColorRequested()
- styledText() checks colorsEnabled() before adding escape codes
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Plain-text help output | Colored help via formatter_fn + styledText() | Phase 20 | Zero behavioral change except visual; all parsing identical |
| No version flag | --version with colored output | Phase 20 | New CLI flag; exits immediately |
| 8 MessageKind values | 13 MessageKind values | Phase 20 | Additive extension; existing code unaffected |
| Plain-text error output | Colored errors via println(Error, ...) | Phase 19 (already done) | Already verified; Phase 20 just confirms |

**Deprecated/outdated:**
- None — all existing color paths remain valid. Phase is purely additive.

## Runtime State Inventory

> Phase 20 is a greenfield (new feature) phase — no rename/refactor/migration. This section intentionally empty.

All runtime state (color mode, NO_COLOR detection) is already managed by the existing `terminal` module. The new MessageKind values and --version flag are pure code additions with zero runtime state impact.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Phase 19 formatter_fn maxColLen computation is already correct and computes plain-text widths (no ANSI codes present) | Architecture Patterns, Pitfall 1 | LOW — Phase 19 explicitly excluded color; confirmation is just verifying the existing code doesn't accidentally include escape codes (it doesn't per source review) |
| A2 | `fmt::color::light_cyan` compiles with the project's fmt version | Standard Stack | LOW — VERIFIED in fmt/color.h source from GitHub; the fmt version in xmake (`add_requires("fmt")` without version pin) pulls latest which includes this color |

## Open Questions

1. **Should --version have a short flag like -V?**
   - What we know: D-03 specifies --version in General group after --help, no short flag mentioned
   - What's unclear: Whether -V would conflict with any existing or planned short option
   - Recommendation: Stick with --version only (no short flag) per D-03. CLI11 supports long-only flags naturally.

2. **Should the intro line (currently rendered by helpIntroLine) also be colored?**
   - What we know: Quick task 260509-tjc migrated the intro line into the formatter_fn description. The description is the first text emitted by the formatter_fn lambda.
   - What's unclear: Whether this intro/description line should be Usage-colored (steel_blue+bold)
   - Recommendation: Color the intro line as `MessageKind::Usage` since it's the first line of --help output and serves as a heading. This is consistent with D-01 which specifies "Usage / 行首描述行 → steel_blue + bold."

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| fmt library | ANSI color output | ✓ | 11.x (via xmake) | — |
| CLI11 | formatter_fn, --version flag | ✓ | 2.6.2 | — |
| clang-cl (C++26) | Compilation | ✓ | 19.x | — |
| xmake | Build system | ✓ | 2.9.x | — |
| Windows VT support | Terminal color rendering | ✓ (via enableVirtualTerminal) | — | Plain text fallback via colorsEnabled() |

**Missing dependencies with no fallback:** None — all dependencies already installed and verified by Phase 19 build.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 v3 |
| Config file | none — Catch2 auto-main in tests/test_main.cpp |
| Quick run command | `xmake build tests && xmake run tests` |
| Full suite command | `xmake build tests && xmake run tests && xmake build e2e_tests && xmake run e2e_tests` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| COLR-01 | --help output contains colored usage/option names/groups | smoke | `xmake run tests "[cmd]"` | ❌ Wave 0 — new test needed |
| COLR-01 | --help output has correct column alignment (no ANSI in padding) | smoke | `xmake run tests "[cmd]"` | ❌ Wave 0 — new test needed |
| COLR-02 | MessageKind enum has 13 values; styleFor() returns non-empty for new kinds | unit | `xmake run tests "[terminal]"` | ❌ Wave 0 — extend terminal_tests.cpp |
| COLR-02 | defaultBadgeLabel() returns empty for all 5 new kinds | unit | `xmake run tests "[terminal]"` | ❌ Wave 0 — extend terminal_tests.cpp |
| COLR-03 | Error messages use println(Error) consistently | smoke | `xmake run tests` | ✅ D-06 confirmed — no new tests needed |
| COLR-04 | --version flag parses and outputs colored version string | integration | `xmake run tests "[cmd]"` | ❌ Wave 0 — new test needed |
| COLR-05 | NO_COLOR=1 produces zero ANSI escape codes in --help | smoke | `xmake run tests "[cmd]"` | ❌ Wave 0 — new test needed |
| COLR-05 | colorsEnabled() gates all new color paths | unit | `xmake run tests "[terminal]"` | ✅ Partially — existing terminal tests cover colorsEnabled(); extend for new kinds |

### Sampling Rate
- **Per task commit:** `xmake run tests "[terminal][cmd]"` — fast terminal + cmd tests
- **Per wave merge:** `xmake run tests` — full unit + integration suite
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/cmd_cmd_tests.cpp` — extend with: --version flag test, help text smoke test (contains expected strings), NO_COLOR compliance test (no ANSI escape codes in help when DISABLED)
- [ ] `tests/infra/terminal_tests.cpp` — extend with: styleFor() returns non-empty for new MessageKind values, defaultBadgeLabel() returns "" for new kinds, styledText with new kinds in Always/Never modes
- [ ] `tests/app/app_entry_tests.cpp` — extend with: version output format test ("encro v1.6 (build: YYYY-MM-DD...)")
- [ ] Framework install: already in place (Catch2 via xmake)

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | Offline CLI tool, no authentication |
| V3 Session Management | no | No sessions |
| V4 Access Control | no | No multi-user access control |
| V5 Input Validation | yes | CLI11 validates all CLI input; terminal module uses type-safe enums; no user-controlled color injection |
| V6 Cryptography | no | No cryptographic operations in this phase |

### Known Threat Patterns for CLI Coloring

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| ANSI escape injection via user-supplied strings | Tampering | Option names/descriptions are compile-time string literals, never user input; CLI11 validates flag names |
| Color escape codes in piped output breaking downstream parsers | Denial of Service | `colorsEnabled()` detects non-terminal output (piped) and disables color automatically |
| Environment variable injection (TERM, NO_COLOR) | Spoofing | `noColorRequested()` only checks existence, not value; `TERM=dumb` check is standard; no eval of env var content |

## Sources

### Primary (HIGH confidence)
- [fmtlib/fmt GitHub source] — `include/fmt/color.h` — VERIFIED: all named colors (steel_blue 0x4682B4, light_cyan 0xE0FFFF, light_sky_blue 0x87CEFA, floral_white 0xFFFAF0, golden_rod 0xDAA520, slate_gray 0x708090), emphasis enum, text_style class, fg()/bg() functions
- [no-color.org] — VERIFIED: NO_COLOR standard (check env var, disable color if present and non-empty, regardless of value)
- [cliutils/cli11 Context7] — VERIFIED: formatter_fn signature `std::string(const CLI::App*, std::string, CLI::AppFormatMode)`, `app.formatter_fn()` API
- [fmtlib/fmt Context7] — VERIFIED: `fg(fmt::color::steel_blue) | fmt::emphasis::bold` pattern, ANSI escape generation
- [Source code review] — `src/infra/terminal.h`, `src/infra/terminal.cpp`, `src/cmd/cmd.cpp`, `src/cmd/cmd.h`, `src/app/app_entry.cpp` — VERIFIED: existing enum structure, styleFor() pattern, formatter_fn structure, colorsEnabled() gating, noColorRequested() implementation

### Secondary (MEDIUM confidence)
- [.planning/codebase/ARCHITECTURE.md] — VERIFIED: component responsibilities, data flow, terminal module layer placement
- [.planning/codebase/CONVENTIONS.md] — VERIFIED: PascalCase enum values, additive-only extension pattern, trailing return types
- [.planning/codebase/TESTING.md] — VERIFIED: Catch2 test patterns, test file naming, ScopedTerminalReset pattern

### Tertiary (LOW confidence)
None — all claims verified against primary sources.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all libraries already in project, versions confirmed via source code and xmake.lua
- Architecture: HIGH — formatter_fn structure verified in Phase 19 source code; color injection pattern verified against fmt source
- Pitfalls: HIGH — STATE.md already documents Pitfalls #2 and #7; ANSI padding issue well-understood from Phase 19 research
- NO_COLOR compliance: HIGH — no-color.org standard verified; terminal module already implements it correctly

**Research date:** 2026-05-09
**Valid until:** 2026-06-09 (stable — fmt and CLI11 are mature libraries; terminal module is internal)
