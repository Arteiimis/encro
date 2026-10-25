# Design

## Context

See `proposal.md` — Why. Three things in the current flow shape every decision here:

- `code-review/SKILL.md` already contains the cheaper shape for small diffs (tier S: one reviewer holding all three lenses, running the coverage protocol itself) — the change is to make that shape the only one, and to reuse it for the fix loop.
- `openspec-verify-change` acquires its evidence by searching the codebase for each requirement and scenario. Its only consumer is the code reviewer's Spec lens, and its only durable output is the REMOVED/RENAMED check that `openspec/config.yaml`'s archive guidance reads.
- The pi `reviewer` is a read-only agent whose transcript is persisted in a session file, so a completed reviewer can be revived from that file rather than rebuilt. Verify of the harness, not an assumption: `subagent({action:"resume", id, message})` starts a new child from the stored session context, keeps its agent, model and tool contract, returns a new run id, and rejects ineligible runs (no persisted session, stopped run); a top-level resume is detached and its result is collected with `bg_wait`, while a workflow script awaits `runs.run(key, {resume})` in place.

## Goals / Non-Goals

**Goals:**

- One reviewer per change, plus at most one continuation of it. Planning stage: at most one reviewer, and only for a change that introduces or re-contracts requirements.
- A review record short enough to read at a glance and to fit in the implementation commit.
- The same independence that matters: the reviewer that raised a finding is the one that grades its fix, and neither is the author of the fix.

**Non-Goals:**

- Changing TDD, the apply read-in, archive auto-sync, bulk archive, or the release flow.
- Removing the `openspec-verify-change` skill: it stays installed, runnable on request, and remains the source of its protocol text, which the code reviewer's brief quotes.
- Tuning what the reviewers look for (the three axes, the smell baseline, the leanness tags) — only how many agents carry them.

## Decisions

**D1 — Staffing is one binary rule, not a tier ladder.** Code-only changed lines (`git diff <fixed>...HEAD --shortstat -- . ':(exclude)openspec/**' ':(exclude)docs/**' ':(exclude)*.md'`, tests included): at most 300 → one reviewer holding Standards, Spec and Leanness in three sections; more than 300, or more than 20 changed files → two reviewers (Standards+Leanness, Spec). Alternative rejected: keep the classifier and its policy code — it decided "one or two" behind a paid API call, a hard guard, a tie-break and a three-rule fallback, and its calibration data shows it never under-provisioned, which is exactly what the binary rule also achieves at zero cost. The per-functional-area extra sub-agent allowance goes: two reviewers is the ceiling.

**D2 — The coverage protocol runs once, inside the code reviewer, at every size.** The reviewer receives the change's artifacts and runs the `openspec-verify-change` checks as its first step (requirement → implementation, scenario → test, task counts, REMOVED/RENAMED inversion), then reports its findings under the Spec heading. Alternative rejected: keep the separate verify pass above some size — it reads the codebase for the same evidence the Spec reviewer must read anyway, and the archive guidance needs only its REMOVED/RENAMED verdict, which now travels in the review record.

**D3 — The fix loop resumes the reviewer.** After triage and fixes, the parent resumes the same reviewer with the findings list and `build/.review/fix.patch` and asks for a per-finding verdict (resolved / not resolved / regressed); resume keeps the whole diff in its context, so the round costs a fix patch instead of a re-read of the change. One round, hard cap; unresolved findings and rejected findings go to the user with their reasons. Fallback, when the harness cannot resume (`resume` rejected, no persisted session, or a harness with no such action): spawn a fresh reviewer with the findings and the fix patch **and label it a fallback in the record** — the fallback is the old behaviour, not a new gate. Independence survives: the fixer is the author, the grader is the reviewer that raised the finding.

**D4 — The planning stage is a self-check unless the contract is new.** Four checks, run by the author over the written artifacts: `openspec validate --strict` passes; every `file:line` citation resolves and says what the artifact claims; every scenario has a task and every task a scenario; the capability's existing requirements are listed so a delta cannot silently contradict one. An independent reviewer is dispatched only when the delta introduces a new capability or touches five or more requirements (`### Requirement:` headers, or `FROM:`/`TO:` pairs, plus `MODIFIED` blocks), and then it is one reviewer with the two lenses and no separate verification pass. Grounding: the archived planning findings are dominated by exactly what the self-check covers — a wrong action count, an Impact list missing two files, a citation range that does not span its symbol, a scenario with no task — while the reviewer keeps its value where requirement contracts are new.

**D5 — The record is capped, not quoted.** Per stage, at most five lines in `tasks.md`: the size/tier and agent count, the finding count per lens, the unresolved findings with their one-line reasons, and the fallback label when the fix loop used one. The per-finding quoted verdicts, the `resolved (<commit>)` log and the separate `docs:` recording commit are dropped; what changed is already in the commit that changed it. Rationale: the record exists so another session can see what was decided and what is still open — the reviewer's full report is a working artifact, not an archived one.

**D6 — Docs-only and mechanical diffs stay Exempt, and so does the planning reviewer for them.** Exempt means no reviewer at all; the planning self-check still runs, because it costs four commands.

## Risks / Trade-offs

- [Fewer independent contexts mean fewer independent eyes on the same diff] → the retained reviewer still reports three sections with per-lens findings, and §D4 keeps an independent pass where contracts are new; revisit if a change archives with a requirement no test covers.
- [`resume` is a harness capability, not a guarantee] → the fresh-reviewer fallback is written into the skill, and the record must name which path was used, so a harness without resume cannot silently degrade to "self-verified".
- [One reviewer carries more context: artifacts, diff, coverage] → `build/.review/diff.patch` plus `diff.stat` and paging by offset keep the patch read cheap, and the parent pre-computes the citation and requirement lists instead of leaving the reviewer to derive them.
- [The 300-line threshold is a guess until measured] → it sits near the observed tier-M boundary (423 lines was the largest "M" sample); re-measure on the next ten code changes and move it once, deliberately.
- [Dropping the per-finding verdict log loses the audit trail for rejected findings] → rejections are one line each in the record, with the reason, which is the part a later session needs; the rest was chat.

## Migration Plan

Two commits per repo convention: a `docs:` commit for these artifacts, then one `docs:` commit rewriting `AGENTS.md`, `.agents/skills/code-review/SKILL.md` and `openspec/config.yaml` together — they tell one story and a partial rewrite would leave the ordering rule contradicting the skill. The new flow takes effect from the next change; this change's own review is Exempt (docs-only), so it neither waits for nor exercises it. Rollback is a revert of that single commit.
