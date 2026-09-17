## Context

See `proposal.md` — Why. The relevant current state, gathered by reading the code and
running the binary:

- `src/infra/terminal.cpp` holds four separate switch tables that all key off
  `MessageKind`: `severityPrefix`, `styleFor`, `streamFor`, and the quiet gate in
  `suppressedByQuiet`. A fifth table, `styleForToken`, is a two-value enum kept private
  to the file. `progress.cpp` has a sixth (`resolveColor`, keyed off `Tone`), and
  `logging/setup.cpp` configures a seventh through spdlog's `set_color`.
- Styling is applied by string concatenation. `format()` runs `fmt::format` on the
  already-styled arguments, then `renderMessage` wraps the result in the kind's style.
  `fmt` terminates every styled span with `ESC[0m`, which is a full attribute reset — so
  a styled token inside a styled body truncates the body. Confirmed on a real run:
  `encro -z <dir> -o <out> --color always` prints `ESC[38;2;70;130;180mFound
  ESC[38;2;218;165;32m2ESC[0m file(s) under ...`, with everything after the count in the
  terminal default.
- `indicators` bundles `termcolor`, which autodetects `TERMCOLOR_USE_WINDOWS_API` on
  Windows (nothing in `xmake.lua` overrides it). Progress bars therefore color through
  `SetConsoleTextAttribute` on the primary platform and through ANSI SGR everywhere
  else — so no progress-bar behavior can be specified in terms of escape sequences — and
  `indicators::Color` has no bright variants: `Color::grey` is ANSI 30, not 90.
- `indicators` sets one foreground per bar and emits a single `termcolor::reset` after
  the whole frame (`dynamic_progress.hpp`), never around an individual bar. A bar left
  with `ForegroundColor::unspecified` therefore inherits whichever bar preceded it in the
  same frame rather than falling back to the terminal default. `unspecified` means "do
  not change the current color", not "use the default color".
- `terminal::styledText(Stream, MessageKind, text)` is the second styling entry point
  after `format`/`renderMessage`: eleven call sites in `src/cmd/cmd.cpp` (option names
  and defaults, section headings, the commands section, the app description line, and
  the whole-line brief-tier hint) plus three test uses.
- `resolveColor(tone, colorsEnabled = false)` returns `Color::white`, and
  `progress_bar.hpp` only skips coloring when the option is `Color::unspecified`. With
  `--color never` on a TTY the bar is forced to a fixed foreground.
- Message kinds are declared in `src/infra/terminal.h` with a "Phase 20 additions"
  comment block, and `tests/infra/terminal_tests.cpp` carries a 14-row table (every kind
  except `Summary`) asserting each kind's styled-ness, prefix, and stream.

## Goals / Non-Goals

**Goals:**

- One styling vocabulary: a `Role` enum that is the only source of foreground SGR
  sequences, consumed by messages, styled values, help text, and progress bars.
- Disjoint style spans by construction, so the truncation defect is unrepresentable
  rather than fixed at one call site.
- A palette that stays legible on both light and dark terminal themes without the
  application knowing which it is on.
- Delete the message kinds and token styles that have one call site or none.

**Non-Goals:**

- No theme system, configurable palette, `--theme` flag, or light/dark auto-detection.
  The roles are palette slots; the user's terminal theme is the only theme.
- No truecolor or 256-color path. Detection of color depth is removed as a question
  rather than answered.
- No change to `--color auto|always|never`, the `color` user-config key, `NO_COLOR`,
  stdout/stderr routing, `--quiet` gating, help column math, or log-file contents.
- Not coloring the `-v` verbose echo stream. Its `EchoShortFormatter` writes its own
  text and bypasses spdlog's `%^%l%$` ranges, and the `-vv` path reaches spdlog's
  color sink, whose defaults assign trace=white, debug=cyan and info=green — colors
  outside the role set. The palette spec therefore exempts the echo stream, mirroring
  the exemption `console-output-conventions` already grants it. The sink's warn/err/
  critical configuration is left untouched as well: `LOG_CRITICAL` has no production
  emitter (only tests use it), so its red background is unreachable in user output and
  changing it would add a task and a test for no observable gain.

## Decisions

### D1: A `Role` enum is the single styling vocabulary

`enum class Role { Default, Muted, Accent, Good, Warn, Bad }` lives in
`src/infra/terminal.h`, next to two functions: one mapping a role to
`fmt::text_style` for the message and help paths, and one mapping a role to
`indicators::Color` for the progress-bar path (plus the "no color" answer when styling
is disabled). The bar mapper deliberately sets no font style — see D6. Nothing else in
the codebase emits a foreground SGR sequence.

Alternatives: keeping `MessageKind`, `Tone`, and the token enum separate and fixing only
their colors leaves four mapping tables producing eight visual outcomes from fifteen
kinds — the complexity this change exists to remove. A `Theme` struct with per-surface
overrides was rejected as YAGNI: there is one terminal, and the user configures it, not
encro.

`MessageKind` stays. It is not a color concept: it drives the stream, the quiet gate,
and the printed prefix text, and those behaviors are specified independently. What
disappears is `styleFor(MessageKind)`; `roleFor(MessageKind)` and `styleSiteFor(MessageKind)`
replace it.

### D2: Styling lands on the prefix or the leading verb, never the body

`styleSiteFor(kind)` returns one of three sites:

| Site | Behavior | Kinds |
| --- | --- | --- |
| `None` | no styling at all | `Plain`, `Info`, `Usage`, `Version`, `OptionDesc`, `Prompt`, `Heading` |
| `Prefix` | the `error:` / `warning:` / `hint:` text carries the role; body untouched | `Error`, `Warning`, `Hint` |
| `LeadingVerb` | the message's first word carries the role; the rest is untouched | `Success`, `Summary` |

`OptionName` and `OptionDefault` are styled at their call sites, as they are today, and
map to `Accent` and `Accent` + faint respectively. `OptionGroup` maps to `Default` and is
rendered bold by the help formatter (D5). That is all fifteen kinds; `Prompt` and
`Heading` appear here as `None` even though commit 2 deletes them, so the table stays
exhaustive for commit 1.

Alternatives: keeping whole-body styling and repairing it by re-emitting the enclosing
SGR after every embedded reset was rejected — it is string surgery that requires
knowing the enclosing style at the point of emission, and it preserves the concept that
made the bug possible. Coloring an entire body is available later without a spec change
if a surface needs it, but nothing in the current output does.

Rationale for `LeadingVerb`: it is what the reference implementations do (a green
`Installed`/`Resolved` verb with an otherwise default body), and it gives the run's
result line a color anchor without spending a color on prose.

### D3: `renderMessage` styles at most one span, and never nests

```text
renderMessage(stream, kind, text):
  prefix = severityPrefix(kind)
  if !colorsEnabled(stream) or roleFor(kind) == Default:
      return prefix.empty() ? text : prefix + " " + text
  switch styleSiteFor(kind):
    Prefix      -> styled(prefix) + " " + text
    LeadingVerb -> styled(firstWord(text)) + restOf(text)
```

`firstWord` splits at the first space. If `text` already begins with an escape
sequence, `LeadingVerb` styling is skipped and the caller's span stands.

That guard is not speculative defensiveness; it is how the spec's disjointness
requirement is enforced. `LeadingVerb` kinds accept arbitrary format arguments, so
`println(Success, "{} packed", count(5))` is a legal call whose first "word" is already
an accent span — styling that `Good` would emit a `Good` span containing an `Accent`
span, exactly the nesting the change exists to eliminate. Skipping the style keeps value
styling authoritative over verb styling and costs one comparison. Its test goes through
the public API (a `Success` message whose first argument is a styled value) rather than
testing a helper.

Because no body is ever wrapped, spans produced by `path()` / `count()` / the accent
helper cannot be nested by construction.

### D4: Styled values are one role

`terminal::path()`, `terminal::count()`, and `terminal::value()` collapse into a single
accent-styled helper. The three former colors — pale sky blue for paths, golden rod for
counts, floral white for values — carried no distinction a user acts on; the last had a
single call site. Separating paths from counts also fails the light-background
requirement, which is what the old palette got wrong.

### D5: Help structure uses bold

`MessageKind::OptionGroup` is rendered with `fmt::emphasis::bold` and no color, in
`formatHelpSection`, `formatGroupHeader`, and the commands section of `formatCommandsSection`.
Headings are the help's skeleton; spending the accent slot on them made them compete
with the option names they introduce. Bold is an attribute, so `--color never` must
suppress it too (see `terminal-color-palette` — Disabled styling).

### D6: `Muted` is a text-only role

The palette spec assigns `Muted` to the terminal's default foreground with faint
emphasis (SGR 2), and no progress bar uses it. Two reasons for the slot choice, then one
for the text-only scope:

1. Themes exist where bright-black equals the background — Solarized Dark sets
   brightBlack to base02, which is its background. `hint: ` would vanish there. Faint
   instead dims whatever foreground the user chose, so its failure mode is "not dimmed",
   not "invisible".
2. It resolves no palette slot, so it cannot be mistaken for a surface that picked its
   own color.
3. A bar cannot carry `Muted` at all. `indicators` sets a foreground before each bar and
   resets once after the whole frame, so a bar with `ForegroundColor::unspecified` keeps
   the preceding bar's color instead of returning to the terminal default. Since a frame
   normally holds an `Accent` overall bar before its slot bars, "idle bar in the default
   foreground" is not expressible — it would render accent by accident. The honest
   answer is to say so and give idle bars `Accent` deliberately (D7).

This is also why D1's bar mapper carries no font style: a role-to-font-style path for
bars would be dead on Windows (`termcolor`'s `dark` has an empty `TERMCOLOR_USE_WINDOWS_
API` branch) and would reintroduce platform divergence.

### D7: `Tone` is deleted; progress bars take a `Role`

`ProgressContext::addBar` and `setTone` take a `Role` instead of a `Tone`, and
`addBar`'s default argument becomes `Role::Accent`. It must not become `Role::Default`:
the old `Tone::Default` meant the ordinary active color, and
`src/organize/pipeline.cpp`'s `addBar("Analyzing")` relies on the default while wanting
an active bar, so a mechanical rename to `Role::Default` would silently strip its color
and contradict the spec's "active bars use Accent". The other bars that pass
`Tone::Overall` do so explicitly and map to `Accent` anyway.

Mapping: `Default`/`Active`/`Overall`/`Idle` → `Accent`, `Packing`/`Finalizing` →
`Warn`, `Success` → `Good`, `Failure` → `Bad`. `Tone::Default` existed only as
`addBar`'s default argument and was already the same color as `Active`; `Overall` was a
fourth blue for a bar that is already distinguished by its label; and `Idle`'s white is
what the light-background defect was made of (D6).

With colors disabled the role resolves to `indicators::Color::unspecified` plus no font
style, replacing today's `Color::white`. That is sound *because every bar in the frame
resolves the same way*: `unspecified` means "do not set a foreground", so a frame in
which no bar sets one stays entirely in the terminal default. It would not be sound for
a single colorless bar among colored ones — hence D6's rule that no role leaves a bar
uncolored in an otherwise colored frame.

With colors disabled the role resolves to `indicators::Color::unspecified` plus no font
style, replacing today's `Color::white`. `unspecified` is what
`progress_bar.hpp` checks before calling `set_stream_color`, so the bar keeps the
terminal's own foreground.

### D8: One accent helper serves paths, counts, and pre-formatted strings

`terminal::path()`, `terminal::count()` and `terminal::value()` collapse onto one accent
helper. `value()` is renamed rather than deleted: it is the only helper that takes an
already-formatted string, and `src/cmd/completion_install.cpp` needs exactly that — it
prints forward-slashed paths produced by its own `forwardSlashes()`, which
`terminal::path()` cannot reproduce because it serializes an `fs::path` back to
backslashes on Windows. `path()` and `count()` become thin wrappers over the helper, and
the completion installer routes its console path arguments through it, so paths are
accented everywhere instead of everywhere except that file.

What disappears is the `value` **style** — the floral-white near-white — not the ability
to accent a string.

### D9: Two commits, one change

Commit 1 converges the palette: roles, both role mappers, the message table (with the
kinds that commit 2 will delete already mapped to `Default` + `None`), the token-helper
collapse and rename, the tone-to-role mapping, the help heading weight, the
`unspecified` fix, the completion-installer path styling, and the run-summary grouping.
Commit 2 removes the dead enum surface: `Heading`, `Usage`, `Version`, and `Prompt`,
with their switch entries.

They are separate functional areas — a revert of commit 2 restores the old enum surface
without touching the palette, and a revert of commit 1 restores the old palette without
touching the surface. Committing them together would make either revert all-or-nothing.
Both commits are independently buildable and green, and each checks off only the tasks
it completes. Keep the split legible: the rename of `terminal::value()` and the removal
of `styleForToken` belong to commit 1 because commit 1 introduces the helper that
replaces them — putting them in commit 2 would leave commit 1 calling a helper it had
already deleted. Planning artifacts (`proposal.md`, `specs/`, `design.md`) go in their
own `docs:` commit first, per the repository's commit discipline.

### D10: The per-kind expectation table is replaced by behavior assertions

`tests/infra/terminal_tests.cpp` currently asserts a fixture table of `{kind, styled?,
prefix, stream}` — a structural assertion that goes red on any enum edit and stays green
when rendering breaks. It is replaced by assertions on behavior that can regress: the
severity prefix text survives `--color never`; a styled line contains exactly one
severity marker; every span in a rendered line is disjoint; prose outside a value span
renders in the default foreground; a leading verb is not styled when the message
already starts with a styled value (D3). That last set is what makes the truncation
defect impossible to reintroduce.

### D11: `styledText` becomes one primitive over named styles

`terminal::styledText(Stream, MessageKind, text)` cannot survive the role table: it keys
off a message kind, and its callers style things that are not messages (option names,
section headings, a whole-line hint). It is replaced by one primitive plus named
accessors:

- `styled(Stream, fmt::text_style, text)` — the only place that wraps text in a style
  and checks `colorsEnabled`.
- `roleStyle(Role)` and `boldStyle()` — the only sources of a `fmt::text_style`.
- `accent(text, Stream)` — the token helper from D8, expressed as
  `styled(stream, roleStyle(Role::Accent), text)`.

The eleven `cmd.cpp` call sites then read: option and subcommand names as `accent(...)`,
option defaults as `styled(stream, roleStyle(Role::Accent) | emphasis::faint, ...)`,
section headings as `styled(stream, boldStyle(), ...)`, the brief-tier hint as
`styled(stream, roleStyle(Role::Muted), ...)`, and the three sites that pass text through
unstyled (`OptionDesc`, the app description line) collapse to the plain string. The two
`styledText` tests in `tests/infra/terminal_tests.cpp` and the one in
`tests/test_utils_tests.cpp` move to `styled`.

Alternatives: keeping a kind-keyed `styledText` alongside the role table leaves a second
mapping from kinds to colors, which is the duplication this change removes; adding a
second attribute parameter to it would make every caller pass an attribute it does not
have. Using the message kind to mean "the help's name style" is exactly the conflation
that produced fifteen kinds for eight visual outcomes.

## Risks / Trade-offs

- **Faint (SGR 2) is a no-op in some terminals** → `hint: ` renders as normal
  foreground. Hierarchy is lost, legibility is not. Accepted explicitly over the
  bright-black slot, which has an invisible failure mode on real themes (D6).
- **An idle bar is no longer visually distinct from an active one** → intentional
  (D6/D7): the bar library cannot express a per-bar default inside a colored frame, and
  an idle slot already names itself (`Encoding: [idle-3]`) at zero progress. The
  alternative, today's fixed white, is what the light-background defect was made of.
- **The leading-verb guard is one branch on an input the current call sites never
  produce** → kept deliberately: the spec requires disjoint spans for any console line,
  and the guard is that requirement's enforcement, not a feature (D3).
- **`completion install` output is covered by opt-in tests only** → its path styling
  change is verified manually with `ENCRO_TEST_COMPLETION=1`, and the accent helper it
  uses is covered by the shared token test.
- **The verbose echo stream still uses colors outside the role set** → exempt in the
  palette spec, matching `console-output-conventions`'s existing echo exemption.
- **Some terminals render bold as bright-intense color** → help headings read as a
  brighter default rather than as their own color. Still not a role color, so the
  palette contract holds.
- **Removing four message kinds touches three production call sites across three files,
  plus one kind with none** → mechanical, and the compiler finds every one; the
  surviving kind for each site is chosen by matching the old behavior (stream, quiet
  gate, prefix).
- **The attention-group wording change is user-visible text** → it is a spec delta
  (`console-output-conventions`), not a silent edit, and it keeps the group on stdout so
  the existing redirection scenario stays true.
- **Two commits for one change** → each is green on its own and checks off only its own
  tasks, so no intermediate state has code absent while its tasks read complete.

## Migration Plan

No data or configuration migration: the change touches neither the job-state file nor
any user-config key, and `--color` keeps its three values and its precedence. Rollback
is `git revert` of commit 2 (restores the removed kinds and helpers) or of commit 1
(restores the previous palette), in that order if both are needed.

Verification for the release: the reviewer-facing check is that
`encro -hh --color always | grep -c '38;2;'` is zero and that a run whose stdout is
redirected produces byte-identical text to the same run before the change, since
styling must not affect line breaking or wrapping.

## Open Questions

- Should the `-v` verbose echo stream adopt the roles for its `info:` / `warning:`
  level tags? It would require moving level coloring out of spdlog's pattern and into
  `EchoShortFormatter`. Deferred: the echo stream is explicitly exempt from
  `console-output-conventions`, so the specs hold either way, and answering it later
  changes only that formatter.
