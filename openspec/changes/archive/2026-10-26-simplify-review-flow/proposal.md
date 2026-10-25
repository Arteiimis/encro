# Proposal

## Why

One change currently spawns four to seven fresh-context agents: a planning reviewer plus its verification pass, an `openspec-verify-change` pass, two or three code reviewers, and one verifier for every fix round. Each of them re-derives the same ground truth — requirement → code, scenario → test — and none of them sees what the previous one already established. Measured on the archived changes: `batch-confirmation-summary` (3 files, 423 added code lines) dispatched 4 agents and produced ~25 findings; `add-command-templates` (tier L) dispatched 7. The `tasks.md` record for that 423-line change runs 52 lines — an eighth of the diff, mostly per-finding verdicts — and across the sampled changes the bookkeeping produced its own `docs:` commits (`record the code review outcome`, `correct the rejected-finding rationale`). The flow buys independence from fresh contexts six times over where once is enough.

## What Changes

- **Planning stage becomes a self-check gate.** The author runs the mechanical half (`openspec validate --strict`, every `file:line` citation resolves and says what the artifact claims, the capability's existing requirements listed so a new one cannot silently contradict them) and records the result in at most five lines in `tasks.md`. An independent planning reviewer is kept only for a change that introduces a new capability or touches five or more requirements.
- **The `openspec-verify-change` pass is folded into the code reviewer**, at every tier rather than only tier S. The reviewer runs the coverage protocol (requirement → implementation, scenario → test, task counts, REMOVED/RENAMED checks) before judging the diff; no separate agent, no separate scorecard document.
- **Tiering collapses to one measured rule**: code-only changed lines (tests included, `.md`/`docs/`/`openspec/` excluded) — at most 300 → one reviewer holding all lenses; more → two (Standards+Leanness, Spec). The classifier call, its policy code and the three-rule fallback triad go away; the per-functional-area extra sub-agent allowance goes away with them.
- **The fix loop resumes the reviewer that found the issues** instead of spawning a verifier: same context, per-finding verdicts, hard cap one round (not two). A fresh reviewer seeded with the findings and `build/.review/fix.patch` is the fallback when the harness cannot resume a child.
- **The review record is capped at five lines per stage**: tier and agent count, finding count per lens, unresolved findings with their reasons. Per-finding verdict logs, quoted before/after text and separate recording commits are removed.
- Docs-only and mechanical diffs stay Exempt; the planning self-check still applies to them.

**BREAKING** for the workflow docs, not for the tool: `AGENTS.md`'s verify-before-review ordering, the `code-review` skill's stage table, tiering and converge sections, and `openspec/config.yaml`'s `apply`/`archive` guidance are rewritten to match. The `openspec-verify-change` skill stays installed and can still be run on request.

## Capabilities

skip_specs — the process lives in `AGENTS.md` and `.agents/skills/code-review/SKILL.md`, which are rewritten by this change; a delta spec would be a third copy of the same rules and no CLI behavior changes.

## Impact

- `AGENTS.md`: the four review bullets (review timing, verify ordering, bulk archive, Post-Change Review), including the per-functional-area sub-agent allowance and the verify-first ordering.
- `.agents/skills/code-review/SKILL.md`: stage table, planning stage (reviewer, converge, record), code stage (pre-compute, tiering, verify intake, dispatch, converge).
- `openspec/config.yaml`: `operations.apply` and `operations.archive` guidance, which currently name the verify report.
- This change's own review: docs-only, so Exempt — the planning self-check above plus the `writing-for-agents` and `review-claude-md` reads that govern edits to `AGENTS.md` and a skill.
