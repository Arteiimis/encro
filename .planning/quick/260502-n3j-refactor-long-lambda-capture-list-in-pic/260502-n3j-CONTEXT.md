# Quick Task 260502-n3j: Refactor long lambda capture list in picture_compress.cpp - Context

**Gathered:** 2026-05-02
**Status:** Ready for planning

<domain>
## Task Boundary

Refactor the lambda expression at `src/picture/picture_compress.cpp:111` which has an excessively long capture list (11 variables), harming readability. Extract the lambda body into a standalone function to reduce the capture list to only reference captures.

</domain>

<decisions>
## Implementation Decisions

### Refactoring Approach
- Extract lambda body to a free (local anonymous namespace) function
- Lambda captures reduced to [&ctx, &resultsMutex, &completed, &results, &progressCtx] — 5 reference captures
- Value parameters (inputPath, outputPath, entryName, quality, total, barIndex) passed as function arguments

### Parameter Style
- Use direct types (fs::path, string, int, size_t, etc.) — no wrapping structs
- Keep parameter list explicit and typed

### the agent's Discretion
- Naming of the extracted function
- Whether to keep `taskCtx` parameter (unused in current body — can be omitted)

</decisions>

<specifics>
## Specific Ideas

The target lambda is at lines 111-154 in `compressImageBatch`. The capture list is:
```cpp
[&ctx, inputPath, outputPath, entryName, quality, total,
 &completed, &results, &resultsMutex, &progressCtx, barIndex]
```

The extracted function should live in the anonymous namespace (alongside `truncateForLabel`).

</specifics>
