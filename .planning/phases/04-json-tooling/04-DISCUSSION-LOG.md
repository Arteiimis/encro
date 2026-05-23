# Phase 4: JSON Tooling - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-23
**Phase:** 4-json-tooling
**Areas discussed:** JSON Formatter Implementation, Dual Sink Architecture, JSON Schema Design, CLI Flag Integration, Edge Case Handling, Phase 3 Integration
**Mode:** --auto (all areas auto-selected, recommended options chosen)

---

## JSON Formatter Implementation

| Option | Description | Selected |
|--------|-------------|----------|
| Custom spdlog::formatter subclass | JsonFormatter implementing format(), uses boost::json for serialization | ✓ |
| Post-processing (read .log, convert) | Parse human-readable log file after the fact | |
| spdlog built-in JSON sink | spdlog's experimental json_sink (limited customization) | |

**[auto] Selected:** Custom spdlog::formatter (recommended default)
**Rationale:** TOOL-02 spec explicitly requires this approach. Per-sink formatter in spdlog allows human-readable console + JSON file without modifying logger registration.

---

## Dual Sink / Dual File Architecture

| Option | Description | Selected |
|--------|-------------|----------|
| Companion .ndjson file | Separate file alongside .log, same timestamp prefix | ✓ |
| Replace existing .log | JSON instead of human-readable file | |
| Single file, dual extension | spdlog writes to both from one sink (not possible) | |

**[auto] Selected:** Companion .ndjson file (recommended default)
**Rationale:** TOOL-03 spec requires console output stays human-readable. Adding a second rotating_file_sink_mt with JsonFormatter is the cleanest spdlog-native approach.

---

## JSON Schema Design

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed fields + optional | timestamp/level/module/source/message always; elapsed_ms/error_context optional | ✓ |
| All fields always present | Use null for missing values | |
| Dynamic schema | Varies by log level or content | |

**[auto] Selected:** Fixed fields always present + optional fields when data exists (recommended default)
**Rationale:** NDJSON consumers expect consistent field set per line. Optional fields (elapsed_ms, error_context) omitted when not applicable — simpler for consumers than null-checking.

---

## CLI Flag Integration

| Option | Description | Selected |
|--------|-------------|----------|
| --log-json independent flag | Works with or without --verbose, follows existing flag pattern | ✓ |
| --log-json implies --verbose | JSON output forces verbose mode on | |
| Replace --verbose with --log-format | New unified flag approach | |

**[auto] Selected:** --log-json as independent flag (recommended default)
**Rationale:** Follows existing --verbose / --verbose-echo pattern. Users may want JSON-only output without console logging, or both together.

---

## Claude's Discretion

- **JsonFormatter file location:** `src/logging/json_formatter.h` — separate header
- **elapsed_ms parsing:** Regex extract from ScopedTimer's "completed in Xms" pattern
- **NDJSON filename:** `fs::path::replace_extension(".ndjson")` on the .log path
- **Performance:** boost::json::serialize() in spdlog async worker thread — non-blocking
- **Retention:** Extend retainRecentLogs() glob to include `encro_*.ndjson*`
- **Error context JSON:** Parse `[context: ...]` suffix from message → `error_context` array
- **Rotating sink:** JSON sink uses same 10MB/3 config; independent rotation per sink

## Deferred Ideas

None — discussion stayed within phase scope.
