# command-templates Specification

## Purpose

Lets a user store the argument cluster that shapes one class of task under a name and replay it with `--template <name>`, so a repeated job is one short command while any single run can still override or clear what the template supplies.

## Requirements

### Requirement: Template files and their location

Each template SHALL be one file inside the templates directory, which is the `templates` directory beside the resolved config file. A template's name SHALL be its file name without the `.json` extension, SHALL be matched case-insensitively, and SHALL consist only of lowercase ASCII letters, digits, `.`, `_` and `-`, starting with a letter or digit. The file name `templates.schema.json` SHALL be reserved.

#### Scenario: Save creates the template in the templates directory

- **WHEN** `ENCRO_CONFIG` points at `<dir>/config.json` and the user runs `encro template save anime --crf 24`
- **THEN** the file `<dir>/templates/anime.json` exists and lists the template

#### Scenario: A name that could escape the directory is rejected

- **WHEN** the user runs `encro template save ../evil --crf 24`
- **THEN** the command exits non-zero with an error naming the illegal name, and no file is written outside the templates directory

#### Scenario: Missing directory means no templates

- **WHEN** the templates directory does not exist and the user runs `encro template list`
- **THEN** the command exits 0 and reports that no templates exist, with no file created

#### Scenario: The schema file is not a template

- **WHEN** the templates directory contains `templates.schema.json` and `anime.json`, and the user runs `encro template list` followed by `encro -T templates.schema <dir>`
- **THEN** only `anime` is listed, and the second command fails with an unknown-template error

#### Scenario: Missing templates directory is an empty set

- **WHEN** the templates directory does not exist and the user runs a run with `-T anime`
- **THEN** the run fails with the unknown-template error rather than a file-system error

### Requirement: Template file format

A template file SHALL be pretty-formatted JSON: one key per line, keys in a stable documented order, and values stored as native JSON types. `$schema` SHALL be a reserved key holding a reference to the template schema; the loader SHALL ignore it, and it SHALL NOT count as a template key. `save` and `unset` SHALL preserve every other key in the file, including a key outside the templateable set, and SHALL rewrite the whole file in canonical form.

#### Scenario: Saved template is canonical JSON with native types

- **WHEN** the user saves a template with `--crf 24 --keep`
- **THEN** the file contains the JSON number `24` and the JSON boolean `true`, indented one key per line, together with a `$schema` reference

#### Scenario: Reserved schema key is not a template key

- **WHEN** a template file contains `$schema` and the user runs `encro template list` and `encro template show <name>`
- **THEN** neither output counts `$schema` as a stored option, and no unknown-key warning names it

#### Scenario: Hand-compacted template is canonicalized

- **WHEN** a template file was hand-edited into compact single-line form and the user runs `encro template save <name> --crf 22`
- **THEN** the file is rewritten in canonical pretty form and the keys it already had are preserved, with `crf` set to 22

### Requirement: Templateable option set

A template SHALL accept only the options that shape a task: every value option and every boolean flag reachable on the main command, plus the subcommand-shared encode options. Input locations (`-i`, `-I`, positional input paths), `--state-file`, `-v` and `-T/--template` itself SHALL NOT be templateable; `-o/--output` SHALL be templateable. The generated template schema SHALL enumerate exactly the templateable options.

#### Scenario: A shape option can be stored

- **WHEN** the user runs `encro template save anime -t picture --compress -q 80 -o +/webp`
- **THEN** the template stores `type`, `compress`, `image-quality` and `output`

#### Scenario: An input path cannot be stored

- **WHEN** the user runs `encro template save anime -i D:\footage`
- **THEN** the command exits non-zero with an error naming `--input` as not templateable, and the template file is unchanged

### Requirement: Selecting a template

`-T`/`--template <name>` SHALL select exactly one template for a run; the option SHALL accept at most one value, and passing it twice SHALL fail. An unknown name SHALL fail with an error naming it and pointing at `encro template list`. `--template` with no value SHALL print the template list and exit 0. Combining `--template` with the `config`, `completion` or `template` subcommands SHALL fail with an error, because those actions never read stored defaults.

#### Scenario: One template per run

- **WHEN** the user runs `encro -T anime -T webp D:\footage`
- **THEN** the run fails during argument parsing

#### Scenario: Unknown name fails with a pointer

- **WHEN** the user runs `encro -T anme D:\footage`
- **THEN** the run fails with an error naming `anme` and mentioning `encro template list`

#### Scenario: Value-less flag lists templates

- **WHEN** the user runs `encro --template`
- **THEN** the template list is printed and the command exits 0

### Requirement: A template is a default layer, not typed arguments

A template value SHALL take effect only when the command line does not express that option, and SHALL be overridden per option rather than as a whole. Because template values are defaults rather than parsed arguments, a template SHALL NOT trip the CLI's cross-option exclusion rules: a template that stores `dry-run` SHALL NOT conflict with an explicit `--crf`, and a template that stores `pack` SHALL NOT conflict with an explicit `--pack-only`.

#### Scenario: Explicit value wins for one option only

- **WHEN** the template stores `crf=24` and `preset=p5` and the user runs `encro -T anime --crf 20 <dir>`
- **THEN** the run uses crf 20 and preset p5

#### Scenario: Stored boolean does not conflict with an excluded flag

- **WHEN** the template stores `dry-run` and the user runs `encro -T anime --crf 20 <dir>`
- **THEN** the run starts without an exclusion error and prints the plan instead of encoding

#### Scenario: Output aliases still resolve against the run's inputs

- **WHEN** the template stores `output` as `+/webp` and the user runs `encro -T webp <dir>` on a different input
- **THEN** the output directory resolves under that run's input root, not under the directory used when the template was saved

### Requirement: Overriding a stored boolean

Every boolean flag a template can store SHALL have a registered negation form wherever the flag is registered, so a stored `true` can be turned off for a single run. A negation form given on the command line SHALL set that option to false regardless of what the template stores, and SHALL render in help as the collapsed `--[no-]name` form.

#### Scenario: Negation turns a stored flag off

- **WHEN** the template stores `keep` and the user runs `encro -T anime --no-keep <dir>`
- **THEN** the run uses the flat output layout

#### Scenario: Every templateable boolean has a negation form

- **WHEN** the user runs `encro -hh`
- **THEN** the lines for `--quiet`, `--log-json`, `--debug`, `--full-progress`, `--resume`, `--restart`, `--video-webp`, `--dry-run`, `--pack-only`, `--yes`, `--pack`, `--compress`, `--keep`, `--recursive` and `--folder-summary` show their collapsed `--[no-]name` form

### Requirement: Template subcommand actions

`encro template` SHALL provide exactly the actions `list`, `show <name>`, `save <name> [options]`, `unset <name> <key>`, `delete <name>`, `path` and `schema`. Each SHALL exit 0 on success; an unknown name, unknown key, wrong argument count or unknown action SHALL exit non-zero with a native-style error message, and bare `encro template` SHALL print the subcommand help and exit 0.

#### Scenario: List shows names with their stored options

- **WHEN** two templates exist and the user runs `encro template list`
- **THEN** each line shows the template name and the options it stores

#### Scenario: Show prints the file content

- **WHEN** the user runs `encro template show anime`
- **THEN** the canonical JSON content of that template file is printed and the command exits 0

#### Scenario: Path prints the templates directory

- **WHEN** the user runs `encro template path`
- **THEN** the templates directory location is printed and the command exits 0

#### Scenario: Delete removes the whole template

- **WHEN** the user runs `encro template delete anime`
- **THEN** the template file no longer exists and `encro template list` no longer names it

#### Scenario: Deleting an unknown template is an error

- **WHEN** the user runs `encro template delete nosuch`
- **THEN** the command exits non-zero with an error naming it

#### Scenario: Wrong argument count fails natively

- **WHEN** the user runs `encro template show` or `encro template save`
- **THEN** the command exits non-zero with a native-style error naming the expected arguments

#### Scenario: Bare command prints the subcommand help

- **WHEN** the user runs `encro template`
- **THEN** the template subcommand help is printed and the command exits 0

### Requirement: Save captures and merges

`save` SHALL capture exactly the templateable options given on its own command line and merge them into the named template, creating it when absent, so keys it does not name are preserved. It SHALL fail without writing when the capture names a non-templateable option or when the capture contains no templateable option, and SHALL print one confirmation line naming the template and its file location on success.

#### Scenario: Second save merges instead of replacing

- **WHEN** a template stores `crf`, `preset` and `keep`, and the user runs `encro template save <name> --crf 22`
- **THEN** the template stores crf 22 and still stores `preset` and `keep`

#### Scenario: Empty capture is rejected

- **WHEN** the user runs `encro template save anime` with no options
- **THEN** the command exits non-zero with an error saying there is nothing to save, and no file is created

#### Scenario: Successful save confirms once

- **WHEN** the user runs `encro template save anime --crf 24`
- **THEN** exactly one confirmation line names the template and the file it was written to

### Requirement: Unset removes one key

`unset <name> <key>` SHALL remove one key from a template so that key falls back to the config value or the built-in default, rewriting the file in canonical form. A key the template does not store SHALL exit 0 without rewriting the file, and a key outside the templateable set SHALL exit non-zero with an error naming it.

#### Scenario: Unset falls back to the config value

- **WHEN** the template stores `crf` and `config.json` stores `crf=23`, and the user runs `encro template unset anime crf`
- **THEN** a later run with `-T anime` uses crf 23

#### Scenario: Unset of an absent key is a no-op

- **WHEN** the template does not store `preset` and the user runs `encro template unset anime preset`
- **THEN** the command exits 0 and the file is unchanged

#### Scenario: Unset of an unknown key is an error

- **WHEN** the user runs `encro template unset anime nosuchkey`
- **THEN** the command exits non-zero with an error naming it, and the file is unchanged

### Requirement: Generated template schema

A JSON schema for template files SHALL be generated from the same option contract that defines the templateable set, describing each templateable key's JSON type, legal values or numeric bounds, built-in default and description, and rejecting unknown keys. It SHALL be written as `templates.schema.json` inside the templates directory on every `save`, and `encro template schema` SHALL print the same content to stdout.

#### Scenario: Schema appears beside the templates

- **WHEN** the user saves a template into a templates directory that has no schema file
- **THEN** `templates.schema.json` exists there and describes the templateable keys

#### Scenario: Schema tracks the build's option contract

- **WHEN** the user removes a templateable key from the schema file by hand and runs `encro template save <name> --crf 24`
- **THEN** the schema file is rewritten and again describes that key

#### Scenario: Printed schema matches the written one

- **WHEN** the user runs `encro template schema` and compares it with the schema file in the templates directory
- **THEN** the content describes the same keys, types and bounds

### Requirement: Schema is produced locally

The template schema SHALL be produced by the binary itself: no run SHALL fetch it over the network, and it SHALL NOT be published as a standalone release artifact.

#### Scenario: Offline save still writes the schema

- **WHEN** a template is saved on a machine with no network access
- **THEN** the schema file is written and describes the full templateable set

### Requirement: Template file diagnostics

A malformed template file SHALL fail the run with an error naming the file and the parse problem, without being overwritten or ignored. A key outside the templateable set found in a hand-edited template SHALL be ignored with a warning naming the template and the key, and the rest of the template SHALL still apply.

#### Scenario: Malformed template fails the run

- **WHEN** a template file contains invalid JSON and the user runs `encro -T anime <dir>`
- **THEN** the run fails with a non-zero exit code and an error naming the file, before any processing starts

#### Scenario: Unknown key warns and is ignored

- **WHEN** a template file contains an unknown key next to `crf`, and the user runs `encro -T anime <dir>`
- **THEN** the run prints one warning naming the template and the unknown key, and still applies `crf`

### Requirement: Verbose attribution of the applied template

When verbosity is raised (`-v` or `-vv`) and a template is applied, the run SHALL print one line naming the template and its file location before processing starts. Without raised verbosity the run SHALL print no template line.

#### Scenario: Verbose run names the template

- **WHEN** the user runs `encro -v -T anime <dir>`
- **THEN** the narration includes one line naming `anime` and the template file path

#### Scenario: Quiet-by-default run stays silent

- **WHEN** the user runs `encro -T anime <dir>` without verbosity flags
- **THEN** no line naming the template is printed
