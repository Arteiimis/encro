---
title: CLI Color Deepening
planted_date: 2026-05-09
trigger_condition: CLI11 migration completed, terminal:: semantic color layer extended with help-section MessageKind values
---

## Idea

Once the CLI11 migration lands and `--help` output has section-based semantic coloring, extend the same approach to other CLI-facing output:

1. **Colored error messages** — parse errors, missing required options, invalid values all use `terminal::println(MessageKind::Error, ...)` consistently
2. **Colored status/warnings** — `--resume` state warnings, file-not-found errors, permission issues
3. **Progress bar color integration** — compact progress bar could use color to indicate success/failure at a glance
4. **`--version` output** — stylized version block with build info colored distinctly

## Rationale

The `terminal::` semantic layer (`MessageKind` → `fmt::text_style`) is already the single source of truth for output style. Extending it is a one-line enum addition + style mapping. The CLI11 migration gives us `formatter_fn` as the injection point for help output. The same pattern applies naturally to error handlers, version output, etc.

## Notes

- Keep it optional/respected: `colorsEnabled()` gate already handles NO_COLOR / piped / TERM=dumb
- No new dependencies — everything already in place
- Should be a separate phase, not bundled with migration to avoid scope creep
