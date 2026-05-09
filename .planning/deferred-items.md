# Deferred Items

Out-of-scope issues discovered during plan execution.

| Category | Item | Status | Discovered At |
|----------|------|--------|---------------|
| Pre-existing test failure | `tests/cmd_cmd_tests.cpp:216` — `CHECK(longestHelpLine(help) <= 72)` fails (COLUMNS=72 test). Pre-existing before quick task 260510-1tv. Not caused by color changes. | Deferred | 2026-05-10, quick-260510-1tv execution |
