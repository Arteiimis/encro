# Architecture Research: CLI11 Migration + Terminal Color Integration

**Domain:** C++ CLI argument parsing migration (boost::program_options → CLI11) with semantic terminal coloring
**Researched:** 2026-05-09
**Confidence:** HIGH

## Standard Architecture

### System Overview (Target v1.6)

```
┌──────────────────────────────────────────────────────────────────────────┐
│                          app_entry.cpp                                    │
│  appentry::run(argc, argv)                                               │
│    ├── prelude::initStartup(argc, argv) ───────────────────────────┐     │
│    │     ├── cmd::commandLineInit(argc, argv) → CmdParseResult      │     │
│    │     ├── terminal::configureFromCliApp(app)                     │     │
│    │     └── prelude::setupLogging(app)                             │     │
│    ├── handleParseAndHelp(startup)                                  │     │
│    │     ├── app.count("--help") → printHelp(app)                   │     │
│    │     └── print colored help via app.help() [formatter_fn]       │     │
│    ├── cmd::buildConfig(app) → AppConfig                            │     │
│    │     ├── app["--type"]->as<string>() replaces vm.at("type")     │     │
│    │     └── app.count("--flag") replaces vm.count("flag")          │     │
│    └── pipeline::run(ctx)                                           │     │
│                                                                     │     │
│  Key change: CmdParseResult.vm (po::variables_map) → CmdParseResult │     │
│  wraps std::unique_ptr<CLI::App> — single parsed-state container    │     │
└──────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────── LAYER DETAIL ───────────────────────────────┐
│                                                                            │
│  cmd/cmd.h                     cmd/cmd.cpp                                │
│  ┌──────────────────────┐      ┌──────────────────────────────────────┐   │
│  │ struct CmdParseResult│      │ CLI::App setup (26 options, 4 groups) │   │
│  │   unique_ptr<CLI::App│      │ resolveHelpTextLayout() preserved     │   │
│  │   optional<string>   │      │ formatter_fn(lambda) → colored string │   │
│  └──────────┬───────────┘      │ try/catch CLI::ParseError             │   │
│             │                  └──────────────────────────────────────┘   │
│             │ passes CLi::App const& to:                                  │
│     ┌───────┼────────┬──────────────┐                                     │
│     ▼       ▼        ▼              ▼                                     │
│  config_  terminal  prelude       utils                                   │
│  builder  .cpp      .cpp          .cpp                                    │
│  (17 vm   (1 vm    (2 vm         (getParamStr                             │
│   calls)   read)    reads)        removed or                              │
│                                   adapted)                                │
└────────────────────────────────────────────────────────────────────────────┘

┌─────────────────── COLORED HELP RENDERING (formatter_fn) ──────────────────┐
│                                                                            │
│  app.formatter_fn([](CLI::App const*, string, AppFormatMode) → string {    │
│    // Builds ANSI-colored help string via terminal::format()               │
│    //                                                                      │
│    // 1. terminal::format(Output, Heading,       "encro: ...\n")           │
│    // 2. terminal::format(Output, Usage,         "Usage: ...\n")           │
│    // 3. For each option group (via get_subcommands()):                    │
│    //    a. terminal::format(Output, OptionGroup,  "{group name}:\n")      │
│    //    b. For each option in group:                                      │
│    //       - Compute plain-text name, pad to max_name_width               │
│    //       - terminal::styledText(Output, OptionName, padded_name)        │
│    //       - terminal::styledText(Output, OptionDesc, description)        │
│    //       - Wrap descriptions exceeding layout.lineLength - name_width   │
│    //    c. terminal::format(Output, Hint,          footer hint)           │
│    return result;                                                          │
│  });                                                                       │
└────────────────────────────────────────────────────────────────────────────┘
```

### Component Responsibilities

| Component | Responsibility | Pre-Change | Post-Change |
|-----------|----------------|------------|-------------|
| `cmd/cmd.h` | Parsed result container | `CmdParseResult{desc,vm,error}` | `CmdParseResult{unique_ptr<CLI::App>, error}` |
| `cmd/cmd.cpp` | Option definition + parsing | boost::po `add_options()` + `store/notify` | CLI11 `add_option()`/`add_flag()` + `app.parse()` |
| `cmd/help_formatter.h` | Colored help string builder | *N/A (new file)* | formatter_fn lambda implementation, column alignment |
| `cmd/config_builder.cpp` | vm → AppConfig transform | `buildConfig(po::vm const&)` | `buildConfig(CLI::App const&)` |
| `infra/terminal.h` | Semantic color layer | 8 MessageKind values | +5: Usage, OptionGroup, OptionName, OptionDesc, Version |
| `infra/terminal.cpp` | Color mode config | `configureFromVariablesMap(vm)` | `configureFromCliApp(CLI::App const&)` |
| `app/prelude.cpp` | Startup orchestration | `setupLogging(vm)` | `setupLogging(CLI::App const&)` |
| `app/app_entry.cpp` | Entry point | `cmd.desc.print(cout)` for help | `cout << cmd.app->help()` (triggers formatter_fn) |
| `utils/utils.cpp` | `getParamStr(vm, name)` | boost::trim_copy + vm.at | Removed; use `app["--name"]->as<string>()` |

## Recommended Project Structure

```
src/
├── cmd/                       # CLI parsing layer (modified files)
│   ├── cmd.h                  # MODIFIED — CmdParseResult with CLI::App
│   ├── cmd.cpp                # REWRITTEN — 138 lines CLI11 option definitions
│   ├── config_builder.h       # MODIFIED — signature change
│   └── config_builder.cpp     # MODIFIED — ~17 vm calls → app accessors
├── infra/                     # Infrastructure (modified files)
│   ├── terminal.h             # MODIFIED — extended MessageKind enum
│   ├── terminal.cpp           # MODIFIED — configureFromCliApp, new styles
│   └── console_width.h/cpp    # UNCHANGED — adaptive width preserved
├── app/                       # Application orchestration (modified files)
│   ├── app_entry.cpp          # MODIFIED — help output via formatter_fn
│   └── prelude.cpp            # MODIFIED — setupLogging takes CLI::App
└── utils/                     # Utilities (modified)
    └── utils.cpp              # MODIFIED — getParamStr removed or adapted
```

### Structure Rationale

- **cmd/ layer unchanged in concept:** still the "parse → validate → build config" pipeline, just with CLI11 types instead of boost::po types. No new subdirectory needed.
- **No new `cmd/help_formatter.cpp` required:** The formatter_fn lambda (15-30 lines) lives inside `cmd.cpp`'s anonymous namespace near the option definitions — keeps formatting logic adjacent to the options it formats. It's called by `app.formatter_fn()` before `app.parse()`.
- **terminal.h extension is additive:** 5 new enum values + 5 styleFor() cases + new defaultBadgeLabel entries. No breaking changes to existing MessageKind consumers.

## Architectural Patterns

### Pattern 1: Parsed State as Single Owning Pointer

**What:** `CmdParseResult` owns a heap-allocated `CLI::App` via `unique_ptr`, moved through `StartupContext`. All consumers receive `CLI::App const&`.

**When to use:** When the parsed CLI state must outlive the parse function but be consumed by multiple downstream callers within a single scope.

**Trade-offs:**
- Pro: Clean ownership — no shared_ptr, no global. App destroyed when `StartupContext` goes out of scope at end of `appentry::run()`.
- Pro: Consumers get read-only access via const ref, preventing accidental mutation.
- Con: Internal CLI11 types leak to consumers — `config_builder.h` now includes `<CLI/CLI.hpp>` instead of `<boost/program_options/variables_map.hpp>`. This is acceptable because CLI11 is a header-only, stable, standard library for CLI parsing.

**Example:**
```cpp
// cmd.h
#include <CLI/CLI.hpp>
#include <memory>
#include <optional>
#include <string>

struct CmdParseResult {
  std::unique_ptr<CLI::App> app;
  std::optional<std::string> error;
};

auto commandLineInit(int argc, char* argv[]) -> CmdParseResult;

// cmd.cpp
auto commandLineInit(int argc, char* argv[]) -> CmdParseResult {
  auto app = std::make_unique<CLI::App>(
    "encro: Universal video encoder/converter/packer"
  );

  // ... add 26 options into 4 option groups ...

  auto error = std::optional<std::string>{};
  try {
    app->parse(argc, argv);
  } catch (CLI::ParseError const& e) {
    error = e.what();
  }

  return {std::move(app), error};
}
```

### Pattern 2: formatter_fn with Direct terminal::println Integration

**What:** CLI11's `formatter_fn` callback builds the entire help string with embedded ANSI color codes using the `terminal::` semantic layer. The callback is set before `app.parse()` and CLI11 invokes it when `--help` triggers (or when help is explicitly requested).

**When to use:** When you need complete control over help output formatting with semantic coloring, and CLI11's built-in formatter labels aren't sufficient.

**Trade-offs:**
- Pro: Full control — per-section colors, adaptive column width, custom layout.
- Pro: Reuses `terminal::` semantic layer — zero new color dependencies.
- Pro: Returns a string, so CLI11's normal help flow (exit code, output stream) works.
- Con: Must manually compute column alignment (option name width padding). CLI11's built-in formatter does this automatically, but formatter_fn replaces all built-in formatting.
- Con: ANSI escape codes are invisible characters — padding must be computed on plain text BEFORE color application to avoid misalignment.

**Example:**
```cpp
app->formatter_fn(
  [layout](CLI::App const* app_ptr, std::string, CLI::AppFormatMode) -> std::string {
    using enum terminal::MessageKind;
    auto result = std::string{};
    auto const w = layout.lineLength;
    auto const minDesc = layout.minDescriptionLength;

    // ── HEADING ──
    result += terminal::format(Stream::Stdout, Heading, "{}\n\n",
      appentry::helpIntroLine());

    // ── USAGE ──
    result += terminal::format(Stream::Stdout, Usage, "Usage: encro [OPTIONS]\n\n");

    // ── OPTION GROUPS ──
    for (auto const* group : app_ptr->get_subcommands()) {
      auto const& name = group->get_name();
      if (name.empty() || name.front() == '+') continue;
      auto const& desc = group->get_description();

      result += terminal::format(Stream::Stdout, OptionGroup, "{}\n", name);
      if (!desc.empty()) {
        result += terminal::format(Stream::Stdout, OptionDesc, "  {}\n", desc);
      }
      result += '\n';

      // Compute max option name width for this group
      auto nameWidth = size_t{10};
      for (auto const* opt : group->get_options()) {
        nameWidth = std::max(nameWidth, opt->get_name().size());
      }

      for (auto const* opt : group->get_options()) {
        auto const optName = opt->get_name();
        auto const padded = optName + std::string(nameWidth - optName.size(), ' ');
        auto const coloredName = terminal::styledText(
          Stream::Stdout, OptionName, padded);
        auto const coloredDesc = terminal::styledText(
          Stream::Stdout, OptionDesc, opt->get_description());

        result += std::format("  {}  {}\n", coloredName, coloredDesc);
      }
      result += '\n';
    }

    return result;
  }
);
```

### Pattern 3: Option Access via CLI::App operator[] and count()

**What:** Replace `vm.count("name")` with `app.count("--name")` and `vm.at("name").as<T>()` with `(*app["--name"]).as<T>()`.

**When to use:** Everywhere the old `po::variables_map` was consulted.

**Trade-offs:**
- Pro: String-based lookup is identical in concept — call sites read the same.
- Pro: `Option*` returned by `app["--name"]` is stable across the App's lifetime.
- Pro: CLI11's `.as<T>()` does type conversion internally, same as boost::po.
- Con: CLI11 option long-names use `--` prefix in operator[]; short names use `-`. Must be consistent: if option is defined as `"--input,-i"`, use `app["--input"]`.

**Example (config_builder.cpp before → after):**
```cpp
// BEFORE (boost::program_options)
config.resumeState = vm.count("resume") > 0;
config.processType = readProcessType(vm);
auto const jobs = vm.at("jobs").as<std::size_t>();

// AFTER (CLI11)
config.resumeState = app.count("--resume") > 0;
config.processType = readProcessType(app);
auto const jobs = app["--jobs"]->as<std::size_t>();
```

## Data Flow

### Request Flow (v1.6 target)

```
User invokes: encro -i ./videos --pack --verbose
    ↓
appentry::run(argc, argv)
    ↓
prelude::initStartup(argc, argv)
    ├── cmd::commandLineInit(argc, argv)
    │     ├── resolveHelpTextLayout() → layout{lineLength, minDescriptionLength}
    │     ├── CLI::App app("encro: ...")
    │     ├── app.add_option_group("General options")
    │     │     ├── app.add_flag("--help,-h", ...)
    │     │     ├── app.add_flag("--verbose,-v", ...)
    │     │     └── ... (6 options total)
    │     ├── app.add_option_group("Input/Output options")
    │     │     ├── app.add_option("--input,-i", ...)
    │     │     └── ... (10 options total)
    │     ├── app.add_option_group("Processing options")
    │     │     └── ... (7 options total)
    │     ├── app.add_option_group("File operation options")
    │     │     └── ... (3 options total)
    │     ├── app.formatter_fn(helpFormatterLambda)
    │     ├── app.parse(argc, argv)  → populates parsed state in App
    │     └── return CmdParseResult{unique_ptr<CLI::App>, error}
    │
    ├── terminal::configureFromCliApp(*app)
    │     └── app.count("--color") → app["--color"]->as<string>() → configure(mode)
    │
    └── prelude::setupLogging(*app)
          └── app.count("--verbose") / app.count("--verbose-echo")
    ↓
StartupContext{cmd, verboseLogFilePath}
    ↓
handleParseAndHelp(startup)
    ├── cmd.error.has_value()? → printError + help hint
    └── app.count("--help")?   → cout << app.help() [triggers formatter_fn] → return 0
    ↓
cmd::buildConfig(app) → AppConfig
    └── 17 app.count()/app["--name"]->as<T>() calls
    ↓
pipeline::run(ctx)
```

### Key Data Flows

1. **Parse flow:** `argv` → CLI11 App setup → `app.parse()` → `CmdParseResult` (move semantics) → `StartupContext` → consumers via const ref.
2. **Help flow:** User passes `--help` → CLI11 detects help flag → invokes `formatter_fn` lambda → returns ANSI-colored string → CLI11 prints to stdout → exits 0.
3. **Color config flow:** `--color auto|always|never` → `app["--color"]` → `terminal::parseColorMode()` → `terminal::configure(mode)` — synchronous before any output.
4. **Config build flow:** CLI::App const& → 17 option reads → validated AppConfig struct — identical control flow to before, different types.

## Scaling Considerations

Not applicable — this is a CLI tool for batch processing. The architecture concerns are correctness (3033 assertions) and maintainability, not throughput scaling.

| Scale | Architecture Adjustments |
|-------|--------------------------|
| Current (1 user) | Monolith with `unique_ptr<CLI::App>` — fine |
| Future | No scaling dimension — CLI parsing is O(1) user operation |

## Anti-Patterns

### Anti-Pattern 1: Passing Raw Option Pointers Around

**What people do:** Store `CLI::Option*` pointers in `CmdParseResult` and pass them to config_builder, prelude, etc.

**Why it's wrong:** Option pointers are tied to the App's lifetime and option order. If the App is reorganized, pointers change. The const reference pattern is more resilient.

**Do this instead:** Pass `CLI::App const&` to all consumers. Each consumer does `app.count("--name")` or `app["--name"]->as<T>()` using the option's long name string. This decouples consumers from option definition order.

### Anti-Pattern 2: Coloring Inside formatter_fn Before Computing Column Width

**What people do:** Apply ANSI color to option names, THEN pad them to column width.

**Why it's wrong:** ANSI escape codes are invisible characters — `fmt::format(fg(red), "--input")` might be 20+ bytes but the terminal only renders 7 characters. Padding the colored string results in misaligned columns because the padding doesn't account for invisible ANSI bytes.

**Do this instead:** Compute max plain-text option name width first (on `opt->get_name()` raw string). Pad the raw string with spaces to that width. THEN apply color to the already-padded string.

```cpp
// CORRECT order:
auto const rawName = opt->get_name();          // "--input,-i" = 10 chars
auto const paddedName = rawName + spaces(pad);  // "--input,-i     "
auto const coloredName = terminal::styledText(  // color applied LAST
  Stream::Stdout, OptionName, paddedName);
```

### Anti-Pattern 3: Keeping boost::program_options After Migration

**What people do:** Add CLI11 alongside boost::po, migrate incrementally, keep both.

**Why it's wrong:** Two CLI parsing libraries = two option definition surfaces, two parse paths, potential inconsistency. The migration scope is only ~138 lines + ~30 call sites — small enough for a single-pass migration.

**Do this instead:** Single commit replacing all boost::po usage in cmd/ + consumers. Remove `boost::program_options` from xmake.lua. 3033 assertions guard against regression.

### Anti-Pattern 4: Hiding CLI11 Headers from Consumers

**What people do:** Wrap CLI11 types in a project-specific header to avoid "leaking" the dependency.

**Why it's wrong:** CLI11 is a header-only, stable, standard library. Wrapping adds an abstraction layer that must be maintained and tested. Every CLI11 API change would require wrapper updates.

**Do this instead:** Include `<CLI/CLI.hpp>` directly in `cmd.h`. Consumers that need CLI::App const& include the cmd header. This is the same pattern used for `<boost/program_options.hpp>` currently in cmd.h.

## Integration Points

### External Services

| Service | Integration Pattern | Notes |
|---------|---------------------|-------|
| CLI11 (header-only) | `#include <CLI/CLI.hpp>`, xmake.lua `add_requires("cli11")` | v2.4+ recommended for stable formatter_fn API |
| fmtlib (already present) | Via `terminal::` wrapper | No direct fmt usage in cmd/ layer |
| console_width (existing) | `resolveHelpTextLayout()` reused | Unchanged — reads COLUMNS env, Windows API, ioctl |

### Internal Boundaries

| Boundary | Pre-Change | Post-Change | Notes |
|----------|------------|-------------|-------|
| cmd → config_builder | `buildConfig(vm)` | `buildConfig(CLI::App const&)` | Signature change only |
| cmd → terminal | `configureFromVariablesMap(vm)` | `configureFromCliApp(CLI::App const&)` | Renamed for clarity |
| cmd → prelude | `setupLogging(vm)` | `setupLogging(CLI::App const&)` | Signature change only |
| cmd → utils | `getParamStr(vm, name)` | Removed; use `app["--name"]->as<string>()` | Helper no longer needed |
| app_entry → cmd | `cmd.desc.print(cout)` | `cout << cmd.app->help()` | CLI11's help() triggers formatter_fn |

## MessageKind Extension

### New Enum Values (terminal.h)

```cpp
enum class MessageKind {
  // Existing (v1.5)
  Plain, Error, Warning, Success, Info, Hint, Prompt, Heading,

  // Help section semantics (v1.6)
  Usage,          // "Usage: encro [OPTIONS]" line
  OptionGroup,    // "General options:" section headers
  OptionName,     // "--help,-h" option flag specifications
  OptionDesc,     // "produce help message" description text
  Version,        // version output (--version)
};
```

### New styleFor() Entries (terminal.cpp)

```cpp
case MessageKind::Usage:       return fg(fmt::color::steel_blue);
case MessageKind::OptionGroup: return fg(fmt::color::steel_blue) | emphasis::bold;
case MessageKind::OptionName:  return fg(fmt::color::light_cyan);
case MessageKind::OptionDesc:  return fg(fmt::color::light_gray);
case MessageKind::Version:     return fg(fmt::color::steel_blue) | emphasis::bold;
```

### Colors Chosen For

| Kind | Color | Rationale |
|------|-------|-----------|
| Usage | steel_blue | Subtle, matches Heading — frames the help output |
| OptionGroup | steel_blue bold | Same as Heading — section markers should be visually distinct |
| OptionName | light_cyan | High contrast against dark terminals, readable at small sizes |
| OptionDesc | light_gray | Neutral, doesn't compete with option names |
| Version | steel_blue bold | Same as Heading — informational, not an error/warning |

## Adaptive Column Width Preservation

The existing `consolewidth::resolveColumns()` + `resolveHelpTextLayout()` mechanism is preserved unchanged:

```cpp
// IN cmd.cpp (unchanged helper)
auto resolveHelpTextLayout() -> HelpTextLayout {
  auto const lineLength = static_cast<unsigned>(consolewidth::resolveColumns({
    .defaultColumns = 80, // CLI11 default = 80
    .minColumns = 40,
    .maxColumns = 120,
  }));
  return {
    .lineLength = lineLength,
    .minDescriptionLength = lineLength / 2,
  };
}
```

The layout is captured by the formatter_fn lambda before `app.parse()`. Inside the lambda, `layout.lineLength` determines total output width and `layout.minDescriptionLength` is the minimum space reserved for descriptions (column 2). Option name column 1 width is dynamically computed as `max(option name lengths)` per group.

## Error Handling Architecture

```cpp
// CLI11 error handling — replaces boost::po error catch
auto error = std::optional<std::string>{};
try {
  app->parse(argc, argv);
} catch (CLI::ParseError const& e) {
  // e.what() returns CLI11's formatted error message
  // Includes: "Option --foo requires a value", etc.
  error = std::string{e.what()} + "\nRun encro -h for usage.";
}
```

CLI11's `ParseError` hierarchy:
- `CLI::ParseError` — base class (catches all)
- `CLI::RequiredError` — missing required option
- `CLI::ValidationError` — type conversion failure
- `CLI::ExtrasError` — unrecognized option (but we use `allow_extras(false)` default)

The current boost::po code does `allow_unregistered()` to detect unrecognized options manually. CLI11 defaults to rejecting extras, so `app.parse()` handles this automatically. The error message from CLI11 is user-friendly and includes the offending argument.

## Option Definition Reference (26 Options → CLI11)

| # | Current boost::po | CLI11 API | Type | Group |
|---|-------------------|-----------|------|-------|
| 1 | `("help,h", "produce help")` | `app.add_flag("--help,-h", ..., "produce help")` | bool flag | General |
| 2 | `("verbose,v", ...)` | `app.add_flag("--verbose,-v", ..., ...)` | bool flag | General |
| 3 | `("verbose-echo,e", ...)` | `app.add_flag("--verbose-echo,-e", ..., ...)` | bool flag | General |
| 4 | `("full-progress,F", ...)` | `app.add_flag("--full-progress,-F", ..., ...)` | bool flag | General |
| 5 | `("color", pvDefault("auto"), ...)` | `app.add_option("--color", colorStr, ...)->default_str("auto")` | string | General |
| 6 | `("yes,y", ...)` | `app.add_flag("--yes,-y", ..., ...)` | bool flag | General |
| 7 | `("input,i", pv<string>(), ...)` | `app.add_option("--input,-i", inputStr, ...)` | string | I/O |
| 8 | `("inputs,I", pvm<vector<string>>(), ...)` | `app.add_option("--inputs,-I", inputList, ...)->expected(-1)` | vector | I/O |
| 9 | `("output,o", pv<string>(), ...)` | `app.add_option("--output,-o", outputStr, ...)` | string | I/O |
| 10 | `("state-file", pv<string>(), ...)` | `app.add_option("--state-file", stateFileStr, ...)` | string | I/O |
| 11 | `("output-format,f", pvDefault("mp4"), ...)` | `app.add_option("--output-format,-f", formatStr, ...)->default_str("mp4")` | string | I/O |
| 12 | `("flat", ...)` | `app.add_flag("--flat", ..., ...)` | bool flag | I/O |
| 13 | `("keep", ...)` | `app.add_flag("--keep", ..., ...)` | bool flag | I/O |
| 14 | `("force-conflict-handling", pvDefault("y"), ...)` | `app.add_option("--force-conflict-handling", fchStr, ...)->default_str("y")` | string | I/O |
| 15 | `("folder-summary,s", ...)` | `app.add_flag("--folder-summary,-s", ..., ...)` | bool flag | I/O |
| 16 | `("recursive,r", ...)` | `app.add_flag("--recursive,-r", ..., ...)` | bool flag | I/O |
| 17 | `("type,t", pvDefault("video"), ...)` | `app.add_option("--type,-t", typeStr, ...)->default_str("video")` | string | Processing |
| 18 | `("jobs,j", pvDefault(10ull), ...)` | `app.add_option("--jobs,-j", jobsNum, ...)->default_str("10")` | size_t | Processing |
| 19 | `("resume", ...)` | `app.add_flag("--resume", ..., ...)` | bool flag | Processing |
| 20 | `("restart", ...)` | `app.add_flag("--restart", ..., ...)` | bool flag | Processing |
| 21 | `("ffmpeg-path,x", pv<string>(), ...)` | `app.add_option("--ffmpeg-path,-x", ffmpegStr, ...)` | string | Processing |
| 22 | `("compress,c", ...)` | `app.add_flag("--compress,-c", ..., ...)` | bool flag | Processing |
| 23 | `("image-quality,q", pv<int>(), ...)` | `app.add_option("--image-quality,-q", qualityNum, ...)` | int | Processing |
| 24 | `("pack,p", ...)` | `app.add_flag("--pack,-p", ..., ...)` | bool flag | File ops |
| 25 | `("pack-only,z", ...)` | `app.add_flag("--pack-only,-z", ..., ...)` | bool flag | File ops |
| 26 | `("overwrite,w", ...)` | `app.add_flag("--overwrite,-w", ..., ...)` | bool flag | File ops |

### Option Group Setup Pattern

```cpp
auto* general = app->add_option_group("General options");
general->add_flag("--help,-h", showHelp, "produce help message");
general->add_flag("--verbose,-v", verboseFlag, "enable verbose output");
// ... remaining General options via general->add_*

auto* io = app->add_option_group("Input/Output options");
io->add_option("--input,-i", inputStr, "input file or directory path");
// ... remaining I/O options via io->add_*

auto* processing = app->add_option_group("Processing options");
processing->add_option("--type,-t", typeStr, "process type: video(vid)|picture(pic)")
  ->default_str("video");
// ... remaining Processing options via processing->add_*

auto* fileop = app->add_option_group("File operation options");
fileop->add_flag("--pack,-p", packFlag, "pack encoded video outputs into zip files");
// ... remaining File op options via fileop->add_*
```

**Note:** All option variables are bare stack variables (string, bool, int, size_t) captured by reference by CLI11's `add_option`/`add_flag`. After `app.parse()`, they hold the parsed values. For config_builder, we use the `app["--name"]->as<T>()` accessor pattern instead of the bound variables — this keeps the config builder decoupled from option definition order and doesn't require exposing all 26 variables from cmd.cpp.

## Build Order Implications

### Phase A: CLI11 Infrastructure (no color changes)
1. Add CLI11 to xmake.lua (`add_requires("cli11")`, `add_packages("cli11")`)
2. Rewrite `cmd.h` — new `CmdParseResult` with `unique_ptr<CLI::App>`
3. Rewrite `cmd.cpp` — 26 options in CLI11 API, formatter_fn initially returns empty string
4. Update `terminal.h/cpp` — rename `configureFromVariablesMap` → `configureFromCliApp`, new MessageKind values (additive, no breaking change)
5. Update `config_builder.h/cpp` — switch to `CLI::App const&`
6. Update `prelude.cpp` — `setupLogging(CLI::App const&)`
7. Update `app_entry.cpp` — `cout << app.help()` instead of `cmd.desc.print()`
8. Remove `getParamStr` from `utils.cpp`
9. Run 3033 assertions — verify zero behavioral regression

### Phase B: Colored Help + Version + Error Coloring
1. Implement `formatter_fn` lambda with column alignment and `terminal::format()` calls
2. Add `--version` flag with colored output via `MessageKind::Version`
3. Color error output using `terminal::eprintln(Error, ...)` in error paths
4. Verify help output visually (manual) + parse tests pass

**Ordering rationale:** CLI11 migration is the dependency. Coloring depends on CLI11's formatter_fn + terminal.h extension. Phase A must land first so the parsing/validation logic is correct before layering visual polish on top. 3033 assertions verify Phase A alone; Phase B adds visual checks.

## Sources

- **CLI11 official documentation (Context7):** `/cliutils/cli11` — formatter_fn API, App class, option groups, Option accessors — HIGH confidence
- **CLI11 book/formatting:** `https://github.com/cliutils/cli11/blob/main/book/chapters/formatting.md` — formatter_fn callback signature, Formatter subclassing — HIGH confidence
- **CLI11 README:** `https://github.com/cliutils/cli11/blob/main/README.md` — add_option, add_flag, option groups, app["--name"] access — HIGH confidence
- **Existing codebase:** `src/cmd/cmd.cpp`, `src/cmd/config_builder.cpp`, `src/infra/terminal.h/cpp`, `src/app/prelude.cpp`, `src/app/app_entry.cpp`, `src/utils/utils.cpp` — current architecture, MessageKind enum, adaptive width — HIGH confidence
- **Project planning:** `.planning/notes/cli-library-selection.md`, `.planning/PROJECT.md` — migration scope, color approach — HIGH confidence

---

*Architecture research for: CLI11 migration + terminal color integration (v1.6)*
*Researched: 2026-05-09*
