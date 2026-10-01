---
name: code-review
description: Two review stages sharing one protocol. Planning stage — reviews a change's proposal/specs/design/tasks before implementation, through a Coherence and a Ground-truth lens. Code stage — reviews the diff since a fixed point (commit, branch, tag, or merge-base) along three axes (Standards — this repo's documented standards; Spec — does the code match the originating spec; Leanness — what in the diff is over-engineering), with the reviewer count tiered by diff size — one consolidated reviewer on small diffs, parallel sub-agents on large ones. Use for reviewing an OpenSpec change's artifacts, or a branch, a PR, work-in-progress changes, or "review since X".
---

Two stages share one protocol — a fresh, independent reviewer; findings that quote their own evidence; and the fix loop in step 8 below.

| Stage | Input | When |
| --- | --- | --- |
| Planning artifacts | `proposal.md`, delta `specs/**/spec.md`, `design.md`, `tasks.md` | all four written, nothing implemented |
| Code diff | the diff since a fixed point | implementation done and self-verified |

## Planning-artifact review

The stage before implementation. Input: a change's `proposal.md`, its delta `specs/**/spec.md`, `design.md` and `tasks.md` — all four written, none implemented. Reviewing earlier cannot check the contract *between* them; reviewing later pays for their defects in code.

Two lenses, reported separately and in this order — they find different defects, and neither may be dropped silently:

- **Coherence** — the four artifacts against each other. A requirement stated two ways across two deltas, design, or tasks; a capability the change touches but the proposal does not list, or a listed capability with no delta; a scenario no task implements, or a task no scenario asked for.
- **Ground truth** — the artifacts against the repository as it is. Every `<path>:<line>` citation resolves and says what the artifact claims; file paths, symbols, test names, config keys and exit codes exist as described; a claimed behavior is not contradicted by the current main spec or the current code; a mechanism the design proposes is not one the codebase already provides under another name; the proposal's Impact section names the files the change actually needs; each `MODIFIED` block matches the main spec's requirement header and carries the whole existing requirement.

Leanness is absent on purpose: it needs measurements that only exist once code does, so it belongs to the code-diff stage. The structural checks are absent too — `openspec validate --strict` already gates them.

### Pre-compute the mechanical half

Produce these first and pass them in as input. They are cheap, and the reviewer should not spend context re-deriving them.

```sh
openspec validate --strict                                              # structure — a gate, not review work
rg -l "<a file the artifacts name>" openspec/specs                      # named files -> owning capability/spec
rg -n "^### Requirement:" openspec/specs openspec/changes/<name>/specs  # requirements that already exist elsewhere
rg -o "[A-Za-z0-9_./-]+\.(cpp|h|lua|md):[0-9]+" openspec/changes/<name>  # citations to resolve
```

### The reviewer

One reviewer, one pass, fresh context, spawned through whatever sub-agent mechanism the harness provides — do not fan out per lens here: these artifacts are small enough to hold at once, and one cheap reviewer beats four expensive ones. What keeps a lens from masking another is the report's structure and its required quotes, not a separate context.

Brief it with the change directory, the pre-computed lists, and this contract verbatim:

> Report exactly two sections, in this order: `## Coherence`, then `## Ground truth`. Under each, one line per finding: `[severity] what is wrong — <path>:<line>: "<quoted text>" vs <path>:<line>: "<quoted text>"`. Quote **both sides** of every conflict — the artifact's claim and the text it contradicts. Write `none` when a lens is clean, and list whatever you could not check. No summary, no praise, no suggestions section. At most 10 findings, worst first.

### Converge

Triage, fix the artifacts, then verify through the fix loop in step 8 below — the rule that carries over unchanged is that whoever wrote the fix does not grade it. Because every finding quotes both sides, the verifier needs only the findings list and the diff (`git diff <planning-commit>...HEAD -- openspec/changes/<name>`), so it works in any harness. One with a cheap way to continue the reviewer's own context may use that instead, but nothing here depends on it existing.

### Record the outcome in the repository

Append the findings and their verdicts — `resolved (<commit>)` or `rejected: <reason>` — to the change's `tasks.md` in their own section, before the implementation commit. A verdict living only in a chat log cannot be checked from another harness or a later session, and archiving requires `tasks.md` to be complete anyway.

### Skip when

The change has no delta (`.openspec.yaml` sets `skip_specs: true`), or the artifact edit is a typo or one-liner: run `openspec validate --strict` and move on.

## Process — code-diff stage

Review of the diff between `HEAD` and a fixed point the user supplies, along three axes:

- **Standards** — does the code conform to this repo's documented coding standards?
- **Spec** — does the code faithfully implement the originating spec?
- **Leanness** — what in the diff is over-engineering, and what replaces it?

How many reviewers carry the axes is a tier decision (step 3): a small diff goes to **one** reviewer holding all three lenses — what keeps an axis from masking the others is the report's structure and its required quotes, not separate contexts. Tooling findings are pre-computed (step 2) so reviewers spend context only on judgement.

### 1. Pin the fixed point

Whatever the user said is the fixed point — a commit SHA, branch name, tag, `main`, `HEAD~5`, etc. If they didn't specify one, ask for it.

Capture the diff command once: `git diff <fixed-point>...HEAD` (three-dot, so the comparison is against the merge-base). Also note the list of commits via `git log <fixed-point>..HEAD --oneline`.

Before going further, confirm the fixed point resolves (`git rev-parse <fixed-point>`) and the diff is non-empty. A bad ref or empty diff should fail here — not inside the reviewers.

### 2. Pre-compute the mechanical half

Produce these once, in the main agent. They are deterministic and spend zero LLM tokens; a reviewer should never spend context re-deriving them.

```sh
git diff <fixed-point>...HEAD --stat    # file list + size
git log <fixed-point>..HEAD --oneline   # commit subjects
git diff <fixed-point>...HEAD           # first ~1500 chars are the tier signal
xmake fmt -k                            # layout already enforced?
xmake tidy                              # clang-tidy findings, report-only
```

Tidy's findings (or `tidy: clean`) go into the Standards brief verbatim — the reviewer judges them, it does not hunt for them. A tool failure is noted and skipped; it never blocks the review.

### 3. Tier the diff

- **Exempt** — typo, docs-only, one-liner, mechanical refactor: skip the stage entirely and say so.
- **Hard guard** — more than 1500 changed lines or more than 30 changed files: tier **L**, no call needed.
- Otherwise, **one classifier call** in the main agent (codemode), with the stat line, the commit subjects, the changed-file list and the first ~1500 characters of the diff as state:

```js
const jev = await models.getModelOfType('classifier', 'opencode', 'jev-1.13-free'); // needs OPENCODE_API_KEY
const r = await models.classify(jev, { state: { stat, subjects, files, excerpt }, questions: {
  tier: { type: 'choice',
    instructions: 'Pick the code-review tier for this C++ CLI repo commit. Judge by review surface - what a reviewer must actually read and understand - not raw line count. When genuinely unsure, pick the larger tier.',
    criteria: {
      S: 'small: one reviewer, single context. Effective (non-mechanical) change roughly <=200 lines, OR large but purely mechanical (code moves/renames, comment or style shifts, bulk deletions, generated or table data). Shallow per-file edits, single topic.',
      M: 'medium: two reviewers (standards+leanness vs spec). Effective change roughly 200-800 lines, or moderate multi-file surface with real logic changes, or heavy spec cross-checking.',
      L: 'large: three or more reviewers, shard by functional area. Effective change >800 lines of new logic across 3+ modules, or a clearly multi-area feature.' } } } });
```

- **Fallbacks**: no classifier configured, or the call errors — use the **mechanical thresholds** (≤200 changed lines and ≤8 files → S; >800 lines or >20 files → L; else M). Confidence below 0.5 — take the **larger** of the classifier pick and the mechanical pick. Never go below the mechanical pick on a low-confidence call.
- Calibrated 2026-09-30 on 43 repo commits: 88% tier agreement with full-information labels (guard included); misses skew safe — over-provisioning, never under-reviewing. Do not tune the thresholds ad hoc; re-calibrate deliberately if the review mix changes.

### 4. Identify the spec source

Look for the originating spec, in this order:

1. A path the user passed as an argument.
2. An active OpenSpec change: `openspec/changes/<change>/specs/*.md` (delta specs) plus its `proposal.md`. Match by branch name, commit-message references, or the file paths the diff touches.
3. Archived changes under `openspec/changes/archive/` if the work is already merged.
4. If nothing is found, ask the user where the spec is. If they say there isn't one, the **Spec** lens is skipped and the final report notes "no spec available".

### 5. Identify the standards sources

`AGENTS.md` documents how code should be written in this repo (naming, conventions, error handling, testing, commits). Paste its Code Conventions table and Testing rules into the brief as an extract — do not point the reviewer at the whole file.

On top of whatever the repo documents, the Standards lens always carries the **smell baseline** below — a fixed set of Fowler code smells (_Refactoring_, ch.3) that applies even when a repo documents nothing. Two rules bind it:

- **The repo overrides.** A documented repo standard always wins; where it endorses something the baseline would flag, suppress the smell.
- **Always a judgement call.** Each smell is a labelled heuristic ("possible Feature Envy"), never a hard violation — and, like any standard here, skip anything tooling already enforces (clang-format via `xmake fmt -k`, clang-tidy via `xmake tidy`).

Each smell, one line — match it against the diff:

- **Mysterious Name** — a name that doesn't reveal intent → rename; no honest name comes, the design's murky.
- **Duplicated Code** — same logic shape in more than one hunk or file → extract the shared shape.
- **Feature Envy** — a method reaching into another object's data more than its own → move it onto the data.
- **Data Clumps** — same few fields/params travelling together → bundle into one type.
- **Primitive Obsession** — a primitive standing in for a domain concept → give the concept its own small type.
- **Repeated Switches** — same `switch`/`if`-cascade on the same type recurring → polymorphism, or one map both sites share.
- **Shotgun Surgery** — one logical change forcing scattered edits → gather what changes together.
- **Divergent Change** — one module edited for several unrelated reasons → split per reason.
- **Speculative Generality** — abstraction, parameters or hooks for needs the spec doesn't have → delete, inline back.
- **Message Chains** — long `a.b().c().d()` navigation → hide the walk behind one method.
- **Middle Man** — a class or function that mostly delegates onward → cut it, call the target direct.
- **Refused Bequest** — an implementer ignoring most of what it inherits → drop the inheritance, compose.

### 6. Dispatch by tier

Briefs are self-contained: a reviewer starts with a fresh context, no skills loaded, and no memory of this conversation. On tiers S and M paste the diff into the brief instead of having the reviewer fetch it — every avoided fetch round-trip saves a full context re-send. On tier L pass the diff command and let each reviewer fetch it; reduce deleted, binary and rename-only files to `--stat` metadata.

Every finding, on every tier, cites `file:line` and quotes the hunk — that evidence contract is what keeps the fix loop cheap.

- **Tier S — one reviewer, three lenses.** One sub-agent with the Standards half, the Spec brief and the Leanness half below. Contract: report exactly three sections, in this order — `## Standards`, `## Spec`, `## Leanness`; under 150 words per section; at most 10 findings total, worst first.
- **Tier M — two reviewers.** (a) **Standards+Leanness**: both lenses read only the diff and the repo conventions, so they share one context — the Standards half plus the Leanness half, sections `## Standards` and `## Leanness`, under 300 words each. (b) **Spec**: the Spec brief below.
- **Tier L — three reviewers**, one per axis, spawned in a single message so they run concurrently: Standards half alone, Spec brief, Leanness half alone. For large multi-area changes AGENTS.md allows ≤1 extra sub-agent per functional area, edge cases only.

**Standards half** — include the inline diff (S/M) or the diff command (L), the commit list, the tidy findings (`tidy: clean` if none), the conventions extract and the smell baseline from step 5 (the reviewer has no other access to either). Brief:

> Report — per file/hunk where relevant — every place the diff violates a documented standard: cite the standard (file + the rule); and any baseline smell you spot: name it and quote the hunk. Distinguish hard violations from judgement calls — documented-standard breaches can be hard, but baseline smells are always judgement calls, and a documented repo standard overrides the baseline. Skip anything tooling enforces. Under 400 words.

**Spec brief** — include the inline diff (S/M) or the diff command (L), the commit list, and the path or fetched contents of the spec. Brief:

> Report: (a) requirements the spec asked for that are missing or partial; (b) behaviour in the diff that wasn't asked for (scope creep); (c) requirements that look implemented but where the implementation looks wrong. Quote the spec line for each finding. Under 400 words.

**Leanness half** — include the inline diff (S/M) or the diff command (L), the commit list, and the tag rules from the `ponytail-review` skill pasted verbatim — sub-agents inherit neither skills nor ponytail mode, and the tags are the whole output format. Brief:

> List only over-engineering in this diff: reinvented standard library, unneeded dependencies, speculative abstractions, dead flexibility. Correctness, security, and performance are out of scope — other axes own those. Never flag a mandated test or a tooling-enforced rule. One line per finding, then the `net: -<N> lines possible.` score.

### 7. Aggregate

Present the reports under `## Standards`, `## Spec`, and `## Leanness` headings, verbatim or lightly cleaned. Do **not** merge or rerank findings — the axes are deliberately separate (see _Why separate axes_).

End with a one-line summary: total findings per axis, and the worst issue _within each axis_ (if any). Don't pick a single winner across axes — that's the reranking the separation exists to prevent.

### 8. Converge on the findings

The author of a fix never grades it alone. After triaging and fixing the accepted findings — proposal reviews of planning artifacts included — spawn a fresh verification sub-agent with the findings list and the fix diff; it returns a per-finding verdict: resolved / not resolved / regressed (the fix broke something else). Loop until every finding resolves; hard cap 2 fix→verify rounds, then stop and hand the unresolved findings — plus anything rejected during triage, with its justification — to the user. A triage that accepts zero findings needs no verification round.

## Why separate axes

A change can pass one axis and fail the others:

- Code that follows every standard but implements the wrong thing → **Standards pass, Spec fail.**
- Code that does exactly what the issue asked but breaks the project's conventions → **Spec pass, Standards fail.**
- Code that does the right thing in more machinery than it needs → **Standards pass, Spec pass, Leanness fail.**

Reporting them separately stops one axis from masking the others — which is also why one reviewer carrying all three lenses (tier S) still reports three sections.
