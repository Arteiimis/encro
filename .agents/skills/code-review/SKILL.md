---
name: code-review
description: Two review stages. Planning stage — a change's proposal/specs/design/tasks before implementation, through a Coherence and a Ground-truth lens. Code stage — the diff since a fixed point (commit, branch, tag, or merge-base) along three axes: Standards (this repo's documented coding standards), Spec (does the code match what the originating spec asked for?), Leanness (what in the diff is over-engineering?), as parallel sub-agents. Use for reviewing an OpenSpec change's artifacts, or a branch, a PR, work-in-progress changes, or "review since X".
---

Two stages share one protocol — a fresh, independent reviewer; findings that quote their own evidence; and the fix loop in step 6 below.

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

Triage, fix the artifacts, then verify through the fix loop in step 6 below — the rule that carries over unchanged is that whoever wrote the fix does not grade it. Because every finding quotes both sides, the verifier needs only the findings list and the diff (`git diff <planning-commit>...HEAD -- openspec/changes/<name>`), so it works in any harness. One with a cheap way to continue the reviewer's own context may use that instead, but nothing here depends on it existing.

### Record the outcome in the repository

Append the findings and their verdicts — `resolved (<commit>)` or `rejected: <reason>` — to the change's `tasks.md` in their own section, before the implementation commit. A verdict living only in a chat log cannot be checked from another harness or a later session, and archiving requires `tasks.md` to be complete anyway.

### Skip when

The change has no delta (`.openspec.yaml` sets `skip_specs: true`), or the artifact edit is a typo or one-liner: run `openspec validate --strict` and move on.

## Process — code-diff stage

Review of the diff between `HEAD` and a fixed point the user supplies, along three axes:

- **Standards** — does the code conform to this repo's documented coding standards?
- **Spec** — does the code faithfully implement the originating spec?
- **Leanness** — what in the diff is over-engineering, and what replaces it?

Each axis runs as a **parallel sub-agent** so they don't pollute each other's context, then this skill aggregates their findings.

### 1. Pin the fixed point

Whatever the user said is the fixed point — a commit SHA, branch name, tag, `main`, `HEAD~5`, etc. If they didn't specify one, ask for it.

Capture the diff command once: `git diff <fixed-point>...HEAD` (three-dot, so the comparison is against the merge-base). Also note the list of commits via `git log <fixed-point>..HEAD --oneline`.

Before going further, confirm the fixed point resolves (`git rev-parse <fixed-point>`) and the diff is non-empty. A bad ref or empty diff should fail here — not inside the parallel sub-agents.

### 2. Identify the spec source

Look for the originating spec, in this order:

1. A path the user passed as an argument.
2. An active OpenSpec change: `openspec/changes/<change>/specs/*.md` (delta specs) plus its `proposal.md`. Match by branch name, commit-message references, or the file paths the diff touches.
3. Archived changes under `openspec/changes/archive/` if the work is already merged.
4. If nothing is found, ask the user where the spec is. If they say there isn't one, the **Spec** sub-agent will skip and report "no spec available".

### 3. Identify the standards sources

`AGENTS.md` documents how code should be written in this repo (naming, conventions, error handling, testing, commits). Feed its conventions section to the Standards sub-agent.

On top of whatever the repo documents, the Standards axis always carries the **smell baseline** below — a fixed set of Fowler code smells (_Refactoring_, ch.3) that applies even when a repo documents nothing. Two rules bind it:

- **The repo overrides.** A documented repo standard always wins; where it endorses something the baseline would flag, suppress the smell.
- **Always a judgement call.** Each smell is a labelled heuristic ("possible Feature Envy"), never a hard violation — and, like any standard here, skip anything tooling already enforces (clang-format via `xmake fmt -k`).

Each smell reads *what it is* → *how to fix*; match it against the diff:

- **Mysterious Name** — a function, variable, or type whose name doesn't reveal what it does or holds. → rename it; if no honest name comes, the design's murky.
- **Duplicated Code** — the same logic shape appears in more than one hunk or file in the change. → extract the shared shape, call it from both.
- **Feature Envy** — a method that reaches into another object's data more than its own. → move the method onto the data it envies.
- **Data Clumps** — the same few fields or params keep travelling together (a type wanting to be born). → bundle them into one type, pass that.
- **Primitive Obsession** — a primitive or string standing in for a domain concept that deserves its own type. → give the concept its own small type.
- **Repeated Switches** — the same `switch`/`if`-cascade on the same type recurs across the change. → replace with polymorphism, or one map both sites share.
- **Shotgun Surgery** — one logical change forces scattered edits across many files in the diff. → gather what changes together into one module.
- **Divergent Change** — one file or module is edited for several unrelated reasons. → split so each module changes for one reason.
- **Speculative Generality** — abstraction, parameters, or hooks added for needs the spec doesn't have. → delete it; inline back until a real need shows.
- **Message Chains** — long `a.b().c().d()` navigation the caller shouldn't depend on. → hide the walk behind one method on the first object.
- **Middle Man** — a class or function that mostly just delegates onward. → cut it, call the real target direct.
- **Refused Bequest** — a subclass or implementer that ignores or overrides most of what it inherits. → drop the inheritance, use composition.

### 4. Spawn the sub-agents in parallel

Start one sub-agent per axis, all in a single message so they run concurrently, then collect the results. Use whatever sub-agent mechanism the current tool provides.

Briefs must be self-contained: a sub-agent starts with a fresh context, no skills loaded, and no memory of this conversation. Each brief must tell its sub-agent to run the diff command itself (it has its own shell access).

**Standards sub-agent prompt** — include:

- The full diff command and commit list.
- The list of standards-source files you found in step 3, **plus the smell baseline from step 3** pasted in full — the sub-agent has no other access to it.
- The brief: "Report — per file/hunk where relevant — (a) every place the diff violates a documented standard: cite the standard (file + the rule); and (b) any baseline smell you spot: name it and quote the hunk. Distinguish hard violations from judgement calls — documented-standard breaches can be hard, but baseline smells are always judgement calls, and a documented repo standard overrides the baseline. Skip anything tooling enforces. Under 400 words."

**Spec sub-agent prompt** — include:

- The diff command and commit list.
- The path or fetched contents of the spec.
- The brief: "Report: (a) requirements the spec asked for that are missing or partial; (b) behaviour in the diff that wasn't asked for (scope creep); (c) requirements that look implemented but where the implementation looks wrong. Quote the spec line for each finding. Under 400 words."

**Leanness sub-agent prompt** — required unless the change is exempt (typos, docs-only, one-liner or mechanical refactor; if exempt, skip this axis and say so in the report). Include:

- The diff command and commit list.
- The tag rules from the `ponytail-review` skill pasted verbatim — sub-agents inherit neither skills nor ponytail mode, and the tags are the whole output format.
- The brief: "List only over-engineering in this diff: reinvented standard library, unneeded dependencies, speculative abstractions, dead flexibility. Correctness, security, and performance are out of scope — other axes own those. Never flag a mandated test or a tooling-enforced rule. One line per finding, then the `net: -<N> lines possible.` score."

If the spec is missing, skip the Spec sub-agent and note this in the final report.

### 5. Aggregate

Present the reports under `## Standards`, `## Spec`, and `## Leanness` headings, verbatim or lightly cleaned. Do **not** merge or rerank findings — the axes are deliberately separate (see _Why separate axes_).

End with a one-line summary: total findings per axis, and the worst issue _within each axis_ (if any). Don't pick a single winner across axes — that's the reranking the separation exists to prevent.

### 6. Converge on the findings

The author of a fix never grades it alone. After triaging and fixing the accepted findings — proposal reviews of planning artifacts included — spawn a fresh verification sub-agent with the findings list and the fix diff; it returns a per-finding verdict: resolved / not resolved / regressed (the fix broke something else). Loop until every finding resolves; hard cap 2 fix→verify rounds, then stop and hand the unresolved findings — plus anything rejected during triage, with its justification — to the user.

## Why separate axes

A change can pass one axis and fail the others:

- Code that follows every standard but implements the wrong thing → **Standards pass, Spec fail.**
- Code that does exactly what the issue asked but breaks the project's conventions → **Spec pass, Standards fail.**
- Code that does the right thing in more machinery than it needs → **Standards pass, Spec pass, Leanness fail.**

Reporting them separately stops one axis from masking the others.
