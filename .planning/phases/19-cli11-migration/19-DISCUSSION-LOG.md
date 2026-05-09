# Phase 19: CLI11 Migration - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-09
**Phase:** 19-cli11-migration
**Areas discussed:** Migration Strategy, formatter_fn Help Layout, Error Message Compatibility, Test Strategy

---

## Migration Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Adapter shim | boost::po-like compatibility wrapper minimizing call-site changes, temporary abstraction | |
| Direct CLI11 API | Migrate each consumer to CLI11-native access patterns directly, no intermediate layer | ✓ |

**User's choice:** Direct CLI11 API migration. No compatibility shim.
**Notes:** 34 vm references across 5 files. `config_builder.cpp` internal helpers (readProcessType, readOutputLayout, etc.) serve as natural migration boundary. CLI11's `app.count()` and `app[]` are semantically close enough that a shim provides minimal value.

---

## formatter_fn Help Layout

| Option | Description | Selected |
|--------|-------------|----------|
| Monolithic lambda | Single formatter_fn with all layout logic inline | |
| Helper functions + orchestrating lambda | `formatOptionHelp()`, `formatGroupHeader()`, `makeHelpFormatter()` returns the CLI::FormatterFcn | ✓ |

**User's choice:** Helper functions + orchestrating lambda.
**Notes:** Structure designed for Phase 20 coloring — helpers return plain text now, colored text later. `resolveHelpTextLayout()` ported from boost::po context. Estimated 60-80 lines total.

---

## Error Message Compatibility

| Option | Description | Selected |
|--------|-------------|----------|
| Exact match | Wrap/rewrite CLI11 errors to match boost::po wording verbatim | |
| Natural CLI11 messages | Accept CLI11's default error messages as-is | ✓ |

**User's choice:** Accept natural CLI11 messages.
**Notes:** Only parse-level errors affected (cmd.cpp:130 "unrecognised option"). Config-level validation errors in config_builder.cpp are unchanged — they operate on parsed results. If any test asserts on exact CLI framework error wording, that test is fragile and should check error behavior instead.

---

## Test Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Integration through CLI11 for both | Both test files parse real argv through CLI11 | |
| Fixture-based for both | Both test files construct results directly, bypass parser | |
| Integration for cmd, fixture for config | cmd_cmd_tests integration, cmd_config_builder_tests fixture-based | ✓ |

**User's choice:** Integration for cmd parsing tests, fixture-based for config builder tests.
**Notes:** config builder tests should operate against a clean results struct (not CLI11 types), making them true unit tests of validation/transformation logic. cmd tests are end-to-end parser tests. `buildConfig()` signature changes from `variables_map const&` to results struct.

---

## the agent's Discretion

- All four areas had clear recommended approaches that the user accepted. No areas were delegated to agent discretion.
- Folded todo `migrate-cli11.md` into Phase 19 scope automatically (score 0.6, directly scoped).

## Deferred Ideas

None — discussion stayed within phase scope.
