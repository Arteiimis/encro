# Proposal

## Why

Users who process the same class of task repeatedly retype an identical argument cluster on every run (`encro -t video --video-codec hevc_nvenc --preset p5 --crf 24 --min-vmaf 92 --keep --pack -j 8 <dir>`). `encro config` holds only the 18 preference keys and is machine-global, so it cannot express "this shape for anime, that shape for phone clips" without overwriting one with the other, and the `ENCRO_CONFIG` file-swapping workaround is invisible from the CLI. The repetition is the largest remaining daily friction in the tool.

## What Changes

- New `template` subcommand with seven actions mirroring `encro config`'s verb style: `list`, `show <name>`, `save <name> [options]`, `unset <name> <key>`, `delete <name>`, `path`, `schema`. Bare `encro template` prints the subcommand help.
- Templates are stored one file per name at the templates directory beside the config file (`<user-config-root>/encro/templates/<name>.json`), canonical pretty JSON whose keys and values have the same shape as `config.json`. A missing directory means no templates.
- Templateability is a registration-time contract: `save` collects exactly the options registered as templateable, so path inputs (`-i`, `-I`, positional input paths, `--state-file`) and `-v` never enter a template. Every other main-command option does — values, and booleans through their negation form.
- Nine flags gain their negation form so a stored boolean can be turned off for a single run: `--no-quiet`, `--no-log-json`, `--no-debug`, `--no-full-progress`, `--no-resume`, `--no-restart`, `--no-video-webp`, `--no-dry-run`, `--no-pack-only`.
- New `-T`/`--template <name>` selects one template. Explicit command-line values win over the template, the template wins over `config.json`, and `config.json` wins over the built-in default; the template applies only to runs that read stored defaults (`config` and `completion` actions are unaffected, and combining them with `--template` is an error). `--template` with no value prints the template list.
- `template save` captures the templateable options given on its own command line and merges them into the named template, creating it when absent. A non-templateable option in the capture is an error, not a silent drop; a capture with no templateable option is also an error.
- A JSON schema for template files is generated from the same registration-time table and written beside the templates on save; `encro template schema` prints it to stdout. Each template carries `"$schema": "./templates.schema.json"`, and `$schema` is a reserved key the loader ignores.
- `-v` prints one line naming the template that was applied, so "why did this run use crf 24" has an answer.
- Diagnostics mirror `config.json`: an unknown key in a template warns and is ignored, malformed JSON names the file and fails the run, an unknown template name fails with a pointer to `encro template list`.
- Documentation: `docs/public-api.md` gains the templates file section and the negation entries, and `README.md` gains a per-task-template subsection beside the existing config one.

### Out of scope

- No path pinning (input paths stay a command-line argument), no template inheritance or multiple `--template` values, and no "save the run I just did" flag.
- No `organize`-specific options in a template, no `template rename`/`edit`, and no shell completion of template names.
- No JSON schema for `config.json` (same generator, separate change), and no schema distribution through release assets or a network URL.

## Capabilities

### New Capabilities

- `command-templates`: the template file contract (location, shape, reserved `$schema`, unknown-key and malformed-file handling), the templateable option set, the `template` subcommand actions, `--template` selection and its place in the value-precedence order, per-run override, the generated schema, and the negated flag forms that make a stored boolean overridable.

### Modified Capabilities

- `user-config`: the value-precedence requirement gains the template layer, so a configurable option's effective value is command line, then template, then config file, then built-in default.
- `shell-completion`: the requirement enumerating completed subcommands gains `template`.
- `cli-help-layout`: the requirement enumerating which subcommand helps honor the effective color mode gains `template`.

## Impact

- `src/cmd/cmd.h` / `src/cmd/cmd.cpp`: the `--template` option, the nine negation forms, probe-parse plumbing for the selected template, template layering in the default injection, the `template` subcommand registration with its arity validation, and the subcommand array handed to the help formatter.
- `src/cmd/config_store.h`: `KeyDef` gains the schema metadata captured while the option is registered (description, enumerated values, numeric bounds), so the schema and the key table cannot drift.
- `src/cmd/option_specs.h`: the `cfg::TemplateKey` token and the metadata capture inside `registerOne`; `src/cmd/help_layout.{h,cpp}`: the sixth entry in the rendered commands section and the template subcommand help formatter.
- New `src/cmd/template_store.{h,cpp}` (templates directory resolution, name validation, load and write, schema generation) and `src/cmd/template_command.{h,cpp}` (the seven actions).
- `src/app/app_entry.cpp`: dispatch `template` on the probe parse, like `config`.
- `tests/`: a new `tests/cmd_template_tests.cpp` plus assertions for the new negation forms and the template line in help/help-tier tests.
- `docs/public-api.md`, `README.md`.
- No change to packaging, the release pipeline or the Scoop manifest: the schema ships inside the binary and is written to the user's templates directory.
