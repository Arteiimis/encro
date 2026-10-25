# Tasks

## 1. Prerequisites

- [x] 1.1 Read `writing-for-agents` and `review-claude-md` in full before touching `AGENTS.md` or a skill; note which of their rules the rewrite must satisfy (name the two files read in the task's completion note)

## 2. The code-review skill

- [x] 2.1 Rewrite the stage table and the planning stage of `.agents/skills/code-review/SKILL.md`: the four-check self-check as the default, the independent reviewer only when the delta introduces a new capability or touches five or more requirements, the two lenses with no separate verification pass, and the five-line record (design D4, D5, D6)
- [x] 2.2 Rewrite the code stage's pre-compute, tiering, verify intake and dispatch sections: one 300-line binary rule replacing the classifier, guard, tie-break and fallbacks; the coverage protocol run by the reviewer at every size; the per-functional-area allowance removed; two reviewers as the ceiling (design D1, D2)
- [x] 2.3 Rewrite the converge step as a single resumed round: `resume` the reviewer with the findings and `build/.review/fix.patch`, per-finding verdicts, hard cap one round, and the labelled fresh-reviewer fallback when resume is rejected (design D3)
- [x] 2.4 Verify the skill is internally consistent: no surviving reference to tier S/M/L staffing, the classifier, a separate verify report, or a verifier sub-agent — `rg -n 'tier L|classifier|verify report|verifier' .agents/skills/code-review/SKILL.md` returns only intentional mentions

## 3. AGENTS.md

- [x] 3.1 Rewrite the four review bullets (review timing, verify ordering, bulk archive, Post-Change Review) to the new chain: self-check → implementation → one review pass → resumed fix round → archive; drop the verify-first ordering and the `≤1 extra sub-agent per functional area` allowance
- [x] 3.2 Verify no other bullet contradicts the new chain: `rg -n 'openspec-verify-change|verify pass|tier L|functional area' AGENTS.md` returns only the mentions the new text intends, and the archive bullet no longer requires a verify pass before archiving

## 4. Configuration pointers

- [x] 4.1 Rewrite `openspec/config.yaml`'s `operations.apply` and `operations.archive` guidance so they point at the review record instead of a verify report, keeping them pointers only (no restatement of ordering)

## 5. Self-check

- [x] 5.1 Run the design's four self-checks over this change's own artifacts (`openspec validate simplify-review-flow --strict`; every `file:line` citation in `proposal.md`/`design.md` resolves; every design decision has a task; the cited measurements match the archived `tasks.md` records) and record the result in at most five lines under `## Planning self-check` in this file

## Planning self-check

- `openspec validate simplify-review-flow --strict`: valid (`skip_specs` accepted).
- No `file:line` citation appears in the artifacts, so check 2 is vacuous; every path the artifacts name (`AGENTS.md`, `.agents/skills/code-review/SKILL.md`, `openspec/config.yaml`, `build/.review/*`) exists and the archived paths resolve.
- No delta specs (check 3's scenario row is vacuous); every design decision has a task — D1/D2 → 2.2, D3 → 2.3, D4/D5/D6 → 2.1.
- Measurements re-derived from the archive (`2026-10-26-add-batch-confirmation-summary/tasks.md` is 78 lines, 52 of them review record; `2026-10-21-add-command-templates` 41): the proposal's first draft said "5 agents / ~150 lines" and was corrected to 4 agents / 52 lines.

## Workflow follow-up

- Commit these planning artifacts as a `docs:` commit before implementation.
- Implement in one `docs:` commit covering `AGENTS.md`, the skill and `openspec/config.yaml`, then check off the tasks above in the same commit.
- Archive the change without a spec sync (`skip_specs: true`); this change's own review is Exempt, so no reviewer is dispatched for it.
