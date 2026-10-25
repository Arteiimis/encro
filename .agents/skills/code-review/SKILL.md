---
name: code-review
description: Two review stages sharing one protocol. Planning stage — a four-check self-check by the author over proposal/specs/design/tasks, escalating to one independent reviewer only when the change introduces a new capability or touches five or more requirements. Code stage — reviews the diff since a fixed point (commit, branch, tag, merge-base) along three axes (Standards — this repo's documented standards; Spec — fidelity, scope creep and spec coverage against the originating change; Leanness — over-engineering), carried by one reviewer, or two on a large diff, with the fix loop resuming that same reviewer for one round. Use for reviewing an OpenSpec change's artifacts, a branch, a PR, work-in-progress changes, or "review since X".
---

Two stages share one protocol: a reviewer that quotes its own evidence, a report split by lens, and a single fix round that goes back to the reviewer who raised the findings.

| Stage | Input | When |
| --- | --- | --- |
| Planning artifacts | `proposal.md`, delta `specs/**/spec.md`, `design.md`, `tasks.md` | all four written, nothing implemented |
| Code diff | the diff since a fixed point | implementation done and self-verified |

Staffing is one reviewer in both stages. A second appears only under the code stage's size rule — never per functional area, never as a verifier of someone else's fix.

## The child agent

Every dispatch below — the planning reviewer, the code reviewer, the resumed fix round, the fallback — launches the harness's read-only reviewer: `reviewer` under pi (`read`, `grep`, `find`, `ls`, `watchdog_diff`; no shell). Where no such agent exists, spawn a fresh-context sub-agent whose tools are read and search only. Never dispatch a step of this skill to a write-capable agent (`worker`, `scout`, `delegate`): a reviewer that can write stops being an independent verifier, and it can mutate the worktree it is reviewing.

A reviewer has no shell, so everything it needs is materialised before it is dispatched — the diff as a file, the pre-computed lists and reports pasted into the brief. A brief that tells a reviewer to run a command is a broken brief.

**The fix round continues the reviewer, it does not replace it.** A completed child is revived from its own session, keeping its agent, model, tools and the whole diff in context: under pi, `subagent({ action: "resume", id, message })` — a top-level resume is detached, so collect its result with `bg_wait`. Where the harness has no such action, or resume rejects the child (no persisted session, a stopped run), dispatch a fresh reviewer with the findings and the fix patch and **label it a fallback** wherever the round is recorded.

## Planning stage

The default is a self-check by the author, because most planning findings are mechanical and the author can see them without a second context. Four checks over the written artifacts:

1. `openspec validate --strict` passes.
2. Every `<path>:<line>` citation in the artifacts resolves, and the file says what the artifact claims it says (a symbol, a test name, a config key, an exit code).
3. Every scenario has a task and every task has a scenario — a scenario with no task is unimplemented by construction.
4. The capability's existing requirements are listed (`rg -n "^### Requirement:" openspec/specs/<capability>`) so a `MODIFIED` block cannot silently contradict one, and the proposal's Impact names the files the change actually needs.

Escalate to **one independent reviewer** when the delta introduces a new capability or touches five or more requirements (`### Requirement:` headers, `FROM:`/`TO:` pairs, `MODIFIED` blocks, counted across the delta). The reviewer runs two lenses, reported separately and in this order:

- **Coherence** — the four artifacts against each other. A requirement stated two ways across two deltas, design, or tasks; a capability the change touches but the proposal does not list, or a listed capability with no delta; a scenario no task implements, or a task no scenario asked for.
- **Ground truth** — the artifacts against the repository as it is. Every `<path>:<line>` citation resolves and says what the artifact claims; file paths, symbols, test names, config keys and exit codes exist as described; a claimed behavior is not contradicted by the current main spec or the current code; a mechanism the design proposes is not one the codebase already provides under another name; each `MODIFIED` block matches the main spec's requirement header and carries the whole existing requirement.

Leanness is absent on purpose: it needs measurements that only exist once code does, so it belongs to the code stage.

Pre-compute the mechanical half first and pass it in — they are cheap, and the reviewer should not spend context re-deriving them:

```sh
openspec validate --strict                                              # structure — a gate, not review work
rg -l "<a file the artifacts name>" openspec/specs                      # named files -> owning capability/spec
rg -n "^### Requirement:" openspec/specs openspec/changes/<name>/specs  # requirements that already exist elsewhere
rg -o "[A-Za-z0-9_./-]+\.(cpp|h|lua|md):[0-9]+" openspec/changes/<name>  # citations to resolve
```

Brief the reviewer with the change directory, those lists, and this contract verbatim:

> Report exactly two sections, in this order: `## Coherence`, then `## Ground truth`. Under each, one line per finding: `[severity] what is wrong — <path>:<line>: "<quoted text>" vs <path>:<line>: "<quoted text>"`. Quote **both sides** of every conflict — the artifact's claim and the text it contradicts. Write `none` when a lens is clean, and list whatever you could not check. No summary, no praise, no suggestions section. At most 10 findings, worst first.

Converge by resuming that reviewer (see _The child agent_) with the findings and `git diff <planning-commit>...HEAD -- openspec/changes/<name>` written to a file: it returns a per-finding verdict, and one round is the cap.

## Code stage

Review of the diff between `HEAD` and a fixed point the user supplies, along three axes:

- **Standards** — does the code conform to this repo's documented coding standards?
- **Spec** — does the diff faithfully implement the originating change, and does it do anything no requirement asked for? This axis owns spec coverage too: requirement → implementation, scenario → test, task counts, and the REMOVED/RENAMED inversion — the checks `openspec-verify-change` defines, run by the reviewer as its first step.
- **Leanness** — what in the diff is over-engineering, and what replaces it?

### 1. Pin the fixed point

Whatever the user said is the fixed point — a commit SHA, branch name, tag, `main`, `HEAD~5`, etc. If they didn't specify one, ask for it.

Capture the diff command once: `git diff <fixed-point>...HEAD` (three-dot, so the comparison is against the merge-base). Also note the list of commits via `git log <fixed-point>..HEAD --oneline`.

Before going further, confirm the fixed point resolves (`git rev-parse <fixed-point>`) and the diff is non-empty. A bad ref or empty diff should fail here — not inside the reviewer.

### 2. Pre-compute the mechanical half

Produce these once, in the main agent. They are deterministic and spend zero LLM tokens; a reviewer should never spend context re-deriving them.

```sh
mkdir -p build/.review                  # ignored, never committed (PowerShell: New-Item -Force -ItemType Directory build/.review)
git diff <fixed-point>...HEAD --stat    # file list, whole range
# the size that decides the staffing
git diff <fixed-point>...HEAD --shortstat -- . ':(exclude)openspec/**' ':(exclude)docs/**' ':(exclude)*.md'
git log <fixed-point>..HEAD --oneline   # commit subjects
git diff <fixed-point>...HEAD > build/.review/diff.patch    # reviewers' copy; append a pathspec for a generated snapshot
git diff <fixed-point>...HEAD --stat > build/.review/diff.stat
xmake fmt -k                            # layout already enforced?
xmake tidy                              # clang-tidy findings, report-only
```

**Size means code.** Step 3 reads the second stat — insertions plus deletions, tests included on purpose, since they are where a green-but-wrong change shows. `.md`, `docs/` and `openspec/` are out because they are prose a reviewer reads once, not surface to reason about: a generated snapshot once turned a 349-line change into a 2555-line one. A generated file that is not markdown has to be named in the pathspec too. Empty means docs-only — Exempt, not a failed command.

**`build/.review/diff.patch` is the reviewers' only view of the diff.** Append a pathspec for a generated snapshot when the change carries one — a reviewer has no shell to filter it later, and a file a reviewer cannot see is a file it must treat as unreviewed. `diff.stat` carries the file list and per-file counts, so the reviewer reads it first to aim its paging; a patch larger than one `read` page is fine, since `read` takes `offset`/`limit`.

Tidy's findings (or `tidy: clean`) go into the Standards brief verbatim — the reviewer judges them, it does not hunt for them. A tool failure is noted and skipped; it never blocks the review.

### 3. Size the diff

- **Exempt** — typo, docs-only, one-liner, mechanical refactor: skip the stage entirely and say so.
- **One reviewer** — at most 300 changed code lines and at most 20 changed files.
- **Two reviewers** — anything larger: (a) Standards+Leanness, (b) Spec. Two is the ceiling; split by lens, never by functional area.

### 4. Identify the spec source

Look for the originating spec, in this order:

1. A path the user passed as an argument.
2. An active OpenSpec change: `openspec/changes/<change>/specs/*.md` (delta specs) plus its `proposal.md`. Match by branch name, commit-message references, or the file paths the diff touches.
3. Archived changes under `openspec/changes/archive/` if the work is already merged.
4. If nothing is found, ask the user where the spec is. If they say there isn't one, the **Spec** lens is skipped and the final report notes "no spec available".

A diff that belongs to an active change also passes its artifacts (`proposal.md`, delta specs, `design.md`, `tasks.md`) to the reviewer — coverage is judged against them, not against a report of them.

Before dispatching, check the tasks are finished and the requirements the change adds are actually in the diff. A requirement the artifacts say is implemented but the code lacks is a CRITICAL the parent fixes first: a reviewer handed a diff the spec says should not exist yet spends its context on a false baseline.

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

### 6. Dispatch

Briefs are self-contained: a reviewer starts with a fresh context, no skills loaded, no memory of this conversation, and no shell. Every brief names the read-only reviewer (_The child agent_) as the agent to launch and points at `build/.review/diff.patch` and `build/.review/diff.stat` from step 2, telling the reviewer to read the stat first, then page the patch with `offset`/`limit`.

Every finding, on either size, cites `file:line` and quotes the hunk — that evidence contract is what keeps the fix round cheap.

- **One reviewer — three lenses.** One sub-agent holding all three briefs; it runs the Spec axis's coverage checks itself before reporting, one agent and one disposable context. Contract: report exactly three sections, in this order — `## Standards`, `## Spec`, `## Leanness`; under 150 words per section; at most 10 findings total, worst first.
- **Two reviewers.** (a) **Standards+Leanness**: both lenses read only the diff and the repo conventions, so they share one context — the Standards half plus the Leanness half, sections `## Standards` and `## Leanness`, under 300 words each. (b) **Spec**: the Spec brief below.

**Standards half** — include the `build/.review/` paths, the commit list, the tidy findings (`tidy: clean` if none), the conventions extract and the smell baseline from step 5 (the reviewer has no other access to either). Brief:

> Report — per file/hunk where relevant — every place the diff violates a documented standard: cite the standard (file + the rule); and any baseline smell you spot: name it and quote the hunk. Distinguish hard violations from judgement calls — documented-standard breaches can be hard, but baseline smells are always judgement calls, and a documented repo standard overrides the baseline. Skip anything tooling enforces. Under 400 words.

**Spec brief** — include the `build/.review/` paths, the commit list, the change's artifacts (proposal, delta specs, design, tasks) and `.agents/skills/openspec-verify-change/SKILL.md`, whose checks are this lens's coverage half. Brief:

> Run the coverage checks the named skill defines — requirement → implementation with `file:line`, scenario → test, task counts, and the inverted REMOVED/RENAMED checks, where `not verified` is never a pass — and report what they found. Then report only: (a) requirements missing an implementation; (b) behaviour in the diff that no requirement asked for (scope creep); (c) requirements that are implemented, where the implementation still looks wrong. Quote the spec line for each finding. Under 400 words.

**Leanness half** — include the `build/.review/` paths, the commit list, and the tag rules from the `ponytail-review` skill pasted verbatim — sub-agents inherit neither skills nor ponytail mode, and the tags are the whole output format. Brief:

> List only over-engineering in this diff: reinvented standard library, unneeded dependencies, speculative abstractions, dead flexibility. Correctness, security, and performance are out of scope — other axes own those. Never flag a mandated test or a tooling-enforced rule. One line per finding, then the `net: -<N> lines possible.` score.

### 7. Aggregate

Present the reports under `## Standards`, `## Spec`, and `## Leanness` headings, verbatim or lightly cleaned. Do **not** merge or rerank findings — the axes are deliberately separate (see _Why separate axes_).

End with a one-line summary: total findings per axis, and the worst issue _within each axis_ (if any). Don't pick a single winner across axes — that's the reranking the separation exists to prevent.

### 8. Converge on the findings

Triage the findings, then fix the accepted ones — the author of a fix never grades it. Once the fixes are in, resume the reviewer that raised them (_The child agent_) with the findings list and the fix diff written to `build/.review/fix.patch` (same mechanism as step 2); it returns a per-finding verdict: resolved / not resolved / regressed (the fix broke something else). One round is the cap: stop after it and hand the unresolved findings — plus anything rejected during triage, with its justification — to the user. A triage that accepts zero findings needs no round. A fix that changes code a requirement rests on invalidates that requirement's coverage verdict — re-check those requirements, not the whole pass.

## The record

Both stages record their outcome in the change's `tasks.md`, under their own heading, in at most five lines: the size and reviewer count, findings per lens, unresolved findings with their one-line reasons, and `fallback` when the fix round could not resume. What changed is in the commit that changed it; the reviewer's report is a working artifact, not an archived one. Write the record before the implementation commit so archive does not have to reopen the change.

A change with no delta (`.openspec.yaml` sets `skip_specs: true`) or a typo/one-liner edit runs the planning self-check only; docs-only and mechanical diffs add no review stage at all.

## Why separate axes

A change can pass one axis and fail the others:

- Code that follows every standard but implements the wrong thing → **Standards pass, Spec fail.**
- Code that does exactly what the issue asked but breaks the project's conventions → **Spec pass, Standards fail.**
- Code that does the right thing in more machinery than it needs → **Standards pass, Spec pass, Leanness fail.**

Reporting them separately stops one axis from masking the others — which is also why one reviewer carrying all three lenses still reports three sections.
