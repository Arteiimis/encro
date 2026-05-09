# Technology Stack: CLI Help Coloring

**Project:** encro — CLI tool for video/picture encoding and zip packing
**Researched:** 2026-05-09
**Confidence:** HIGH

## Recommended Stack

### Core Coloring Infrastructure (Existing — No Changes)

| Technology | Version | Purpose | Why |
|------------|---------|---------|-----|
| `terminal::` layer (custom) | In-project | Semantic color system: `MessageKind` enum → `fmt::text_style` mapping. Single source of truth for all terminal styling. | Already built, tested, and integrated. Handles NO_COLOR, `--color`, `isatty()`, `TERM=dumb`, Windows VT enable. Zero new development for the gating layer. |
| `fmtlib` (fmt) | ≥11.x (bundled) | ANSI escape code generation: `fmt::fg()`, `fmt::bg()`, `fmt::emphasis`, `fmt::terminal_color`. | Already a project dependency. Used by `terminal::styleFor()` to produce `fmt::text_style` values. No additional dependency. |

### CLI Framework (Migration Target)

| Technology | Version | Purpose | Why |
|------------|---------|---------|-----|
| CLI11 | ≥2.4.x (header-only) | Command-line parsing. Replaces `boost::program_options`. Provides `formatter_fn` lambda for full help customization. | Chosen per `.planning/notes/cli-library-selection.md`. Only CLI library with `formatter_fn` callback that gives full control over help output rendering — the injection point for `terminal::println()` calls. Fluent API replaces disliked `operator()` chaining. |

### Help Layout Utilities (Existing — Carry Forward)

| Technology | Version | Purpose | Why |
|------------|---------|---------|-----|
| `console_width.h` (custom) | In-project | Adaptive column width detection: reads terminal width, provides min/max bounds for help text layout. | Already used by current `boost::program_options` help formatter. Must be carried forward into CLI11 `formatter_fn` to preserve adaptive layout. |

### Removed Dependencies

| Technology | Reason for Removal |
|------------|-------------------|
| `boost::program_options` | Replaced by CLI11. No built-in color support, sealed `print()` API, disliked `operator()` chaining. May retain `boost::uuid` and `boost::lexical_cast` if used elsewhere. |

## Alternatives Considered

| Category | Recommended | Alternative | Why Not |
|----------|-------------|-------------|---------|
| Color library | `terminal::` + `fmtlib` (existing) | **rang** | No NO_COLOR support (open issue #140), known `style::reset` bug (#133). Single-header but semantically weak. |
| Color library | `terminal::` + `fmtlib` (existing) | **Hand-written ANSI escape sequences** | Reinvents wheel. No semantic layer. Scattered magic strings. No NO_COLOR gating. |
| Color library | `terminal::` + `fmtlib` (existing) | **Raw `fmt::fg()` calls everywhere** | Bypasses `terminal::` semantic layer. Duplicates NO_COLOR gating. Violates single source of truth principle. |
| CLI parser | CLI11 | **cxxopts** | No `formatter_fn` equivalent — only `set_width()`, no help rendering hook. Cannot inject `terminal::println()`. |
| CLI parser | CLI11 | **Keep boost::program_options** | No built-in color. Formatter is sealed (can only inject ANSI into description strings, not full layout). Would require walking `options_description::options()` vector manually — same effort as CLI11 migration but with worse API. |

## Installation

```bash
# CLI11 is header-only — add to xmake.lua or as a git submodule
# No new packages to install if using vcpkg/conan with CLI11

# Existing dependencies (already in project):
# fmtlib — already bundled and linked
# boost (uuid, lexical_cast only if still needed after migration)
```

## Version Compatibility

| Component | Min Version | Notes |
|-----------|-------------|-------|
| CLI11 | 2.3.0+ | `formatter_fn` available since v2.0. Formatter subclassing API stable since v1.9. |
| fmtlib | 10.0+ | `fmt::terminal_color`, `fmt::emphasis`, `fmt::styled()` all stable. |
| C++ Standard | C++20 | Required by project. CLI11 is C++11+. fmtlib requires C++20 for `std::format` interop. |
| Windows | 10 (1903+) | Virtual Terminal support required for ANSI. `terminal::colorsEnabled()` already enables VT processing via `SetConsoleMode(ENABLE_VIRTUAL_TERMINAL_PROCESSING)`. |

## Sources

- **CLI11:** Context7 `/cliutils/cli11` — HIGH confidence (official docs). `formatter_fn` API, `CLI::Formatter` subclassing, `App::get_formatter()`, `AppFormatMode` enum.
- **fmtlib:** Context7 `/fmtlib/fmt` — HIGH confidence (official docs). Color API, `text_style`, `fg()`, `bg()`, `emphasis`, `terminal_color`, `rgb()`.
- **Terminal layer:** `src/infra/terminal.h`, `src/infra/terminal.cpp` — HIGH confidence (primary source). `MessageKind` enum, `styleFor()`, `colorsEnabled()`, NO_COLOR handling.
- **CLI library selection:** `.planning/notes/cli-library-selection.md` — HIGH confidence (design authority). CLI11 vs cxxopts vs boost::po comparison, migration scope.
- **NO_COLOR standard:** https://no-color.org/ — HIGH confidence (authoritative). Formal specification, FAQ, 200+ tool adoption list.

---

*Stack research for: encrō v1.6 CLI Color Deepening*
*Researched: 2026-05-09*
