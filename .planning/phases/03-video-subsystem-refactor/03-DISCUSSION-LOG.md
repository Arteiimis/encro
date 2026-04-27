# Phase 3: Video Subsystem Refactor - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions captured in 03-CONTEXT.md — this log preserves the discussion.

**Date:** 2026-04-27
**Phase:** 03-video-subsystem-refactor
**Mode:** discuss (default)
**Areas discussed:** Extraction destination, Captured variable handling, withActionJobState/withJobState nesting, Naming convention + granularity

## Discussion

### Extraction destination
| Question | Options | Selection |
|----------|---------|-----------|
| Where should extracted functions live? | Free functions in anonymous namespace / Private methods on EncodingExecutionContext / New detail header | **Free functions in anonymous namespace** |

### Captured variable handling
| Question | Options | Selection |
|----------|---------|-----------|
| How should captured variables be passed? | Individual parameters / Capture struct | **Individual parameters** |

### withActionJobState/withJobState nesting
| Question | Options | Selection |
|----------|---------|-----------|
| How to handle withActionJobState lambdas inside other lambdas? | Leave 2-level as-is, extract only 3+ levels / Extract all inner bodies | **Leave 2-level, extract 3+ levels only** |
| Extract repeated patterns in runEncodingWithoutProgress? | Yes, follow noteStopRequest precedent / No, only 3+ levels | **Yes, extract repeated patterns** |

### Naming convention + granularity
| Question | Options | Selection |
|----------|---------|-----------|
| Naming convention for extracted functions? | camelCase / underscore_style | **Descriptive camelCase** |
| How aggressively to extract? | Clear 3+ level cases only (~4-5 functions) / Extract everything possible | **3+ level cases only** |

## Corrections Made

No corrections — all recommendations accepted.

## Deferred Ideas

- Refactoring withActionJobState/withJobState to non-lambda pattern — out of scope
- Creating video_batch_execution_detail.h — rejected in favor of anonymous namespace
- Full file structural refactoring beyond lambda nesting
