# encro public API

Semantic Versioning 2.0.0 requires a declared public API before any version number can mean anything: a MAJOR bump is an incompatible change *to that surface*, so the surface has to be written down rather than inferred case by case. This document is that declaration.

Everything listed here is a promise. If a change alters any of it, the version increment follows the table at the end of this file. Anything not listed here is not a promise, and changing it does not, by itself, justify a version increment.

## Command line

encro has five subcommands and a default mode that runs when no subcommand is given.

| Command | Purpose |
| --- | --- |
| *(none)* | Encode, convert, compress or pack the paths given to it |
| `preview` | Render an original-vs-encoded comparison video |
| `organize` | Group a folder of images into per-character folders using a local model |
| `batch` | Run every first-level subfolder of a root directory as its own task |
| `config` | Read and write user-level defaults |
| `completion` | Print, install or uninstall shell completion scripts |
| `template` | Store, inspect and apply per-task argument templates |

`organize` requires a local inference backend, and the Windows builds are the ones that carry it. A build without one does not offer the command: `-h` and the generated completion scripts leave it out, and `encro organize <dir>` exits non-zero with an error naming the missing backend, before scanning, copying or downloading anything. Offering it instead — as releases before this one did, filing every image under `organized/uncategorized/` and exiting 0 — makes the change a **MAJOR** one by the table below, because an exit code changed. A completion script installed before an upgrade keeps its old candidates until `encro completion <shell> --install` is re-run.

`--version` prints one line naming the release the binary was built from. The **value** is a promise: it comes from the project's declared version in `xmake.lua`, never from a literal in source, so it can never name a release that does not exist. The wording and format of that line are **not** a promise — they are rendering, and the "What this declaration does not cover" section below governs them.

### Default mode options

`encro [<input>... | -i <input> | -I <file>...] [-o <output>] [-f mp4|webp] [-r] [-j <n>] [-p] [--resume|--restart]`

| Flag | Argument | Accepted values | Default |
| --- | --- | --- | --- |
| `-h`, `--help` | — | — | — |
| `-hh` | — | — | — |
| `--version` | — | — | — |
| `-T`, `--template` | `<name>` | a stored template name; without a name, lists the templates | — |
| `-v`, `--verbose` | count | repeatable; more repetitions mean more output | `0` |
| `--quiet` | — | negation `--no-quiet` | — |
| `--log-json` | — | negation `--no-log-json` | off |
| `--debug` | — | negation `--no-debug` | — |
| `-F`, `--full-progress` | — | negation `--no-full-progress` | — |
| `--color` | `<mode>` | `auto`, `always`, `never` | `auto` |
| `-y`, `--yes` | — | negation `--no-yes` | off |
| `-i`, `--input` | `<path>` | one input | — |
| `-I`, `--inputs` | `<path>` | repeatable, one per video | — |
| `-o`, `--output` | `<path>` | a directory, or the aliases `+` / `input://` and `=` / `common://` | — |
| `--state-file` | `<path>` | file path | `<work-root>/.encro/job-state.json` |
| `-f`, `--output-format` | `<format>` | `mp4`, `webp` | `mp4` |
| `--keep` | — | negation `--no-keep` | off (flat output) |
| `--force-conflict-handling` | `<value>` | `y` (auto-rename), `n` (allow duplicates) | `y` |
| `-s`, `--folder-summary` | — | negation `--no-folder-summary` | off |
| `-r`, `--recursive` | — | negation `--no-recursive` | off |
| `-t`, `--type` | `<type>` | `video`, `vid`, `picture`, `pic` | `video` |
| `-j`, `--jobs` | `<n>` | integer `>= 1` | `10` |
| `--resume` | — | negation `--no-resume`; mutually exclusive with `--restart` | — |
| `--restart` | — | negation `--no-restart`; mutually exclusive with `--resume` | — |
| `-x`, `--ffmpeg-path` | `<path>` | directory containing `ffmpeg`/`ffprobe` | `PATH` |
| `-c`, `--compress` | — | negation `--no-compress` | off |
| `--video-webp` | — | negation `--no-video-webp` | off |
| `-q`, `--image-quality` | `<n>` | integer `2`–`31`, lower is better | `2` |
| `--crf` | `<n>` | integer `0`–`51`, lower is better | `28` |
| `--min-vmaf` | `<n>` | integer `0`–`100` | `92` |
| `--dry-run` | — | negation `--no-dry-run` | — |
| `--preset` | `<preset>` | `p1`–`p7`, `auto` | `auto` |
| `--video-codec` | `<codec>` | `hevc_nvenc`, `libx265`, `libx264` | `hevc_nvenc` |
| `-p`, `--pack` | — | negation `--no-pack` | off |
| `-z`, `--pack-only` | — | negation `--no-pack-only` | — |

Positional arguments are input paths and are accepted in place of `-i`/`-I`.

### `preview`

`encro preview <original> [<encoded>] [options]`

| Flag | Argument | Accepted values | Default |
| --- | --- | --- | --- |
| `--output` | `<path>` | file path | `<original-dir>/<original-stem>.preview.mp4` |
| `--start` | `<seconds>` | number | — |
| `--duration` | `<seconds>` | number | — |
| `--no-open` | — | — | opens the result |
| `--crf`, `--min-vmaf`, `--preset`, `--video-codec` | — | as in the default mode | as in the default mode |

Positional `original` and `encoded` are required and optional respectively.

### `organize`

`encro organize [dir] [options]`

| Flag | Argument | Accepted values | Default |
| --- | --- | --- | --- |
| `-r`, `--recursive` | — | negation `--no-recursive` | off |
| `--min-confidence` | `<float>` | `0`–`1` | `0.35` |
| `--model-dir` | `<path>` | directory | `~/.encro/models` |
| `--identity-tau` | `<float>` | `0`–`1` | `0.70` |
| `--download-models` | — | — | off |
| `--dry-run` | — | — | — |
| `--recluster` | — | — | — |
| `--ingest` | `<folder>` | repeatable | — |
| `--ignore-folder` | `<folder>` | repeatable | — |

### `batch`

`encro batch <root> [options]` — `root` is required and positional.

Accepts `-o`, `-t`, `-j`, `--resume`, `--restart`, `-x`, `-c`, `--video-webp`, `-q`, `--crf`, `--min-vmaf`, `--dry-run`, `--preset`, `--video-codec`, `-p`, `-z`, `-f`, `--keep`, `--force-conflict-handling`, `-s`, `-r`, `-y` with the same value sets and defaults as the default mode, plus `-o`'s default of `<root>/packed`.

`-I`/`--inputs` and `--state-file` are rejected under `batch`: the positional root replaces input lists, and each subfolder keeps its own state file.

### `config`

`encro config <list | get <key> | set <key> <value> | unset <key> | path>`

`list` prints every key with its effective value and whether that value came from the config file or the built-in default. `path` prints the config file's location. `get`, `set` and `unset` accept only the keys in the table below; an unknown key is an error, and a value that fails the key's validation is rejected before it is written.

### `completion`

`encro completion [--install | --uninstall] <powershell|bash>`

`--install` and `--uninstall` are mutually exclusive; without either, the script is printed to stdout.

### `template`

`encro template <list | show <name> | save <name> [options] | unset <name> <key> | delete <name> | path | schema>`

`save` stores the templateable options given on its own command line, merging them into the named template; `list` prints one line per template with the options it stores; `show` prints the template file; `path` prints the templates directory; `schema` prints the JSON Schema for template files. An unknown template name, an unknown key, or an option that is not templateable is an error. Options after `save <name>` are the parent command's own options (`encro template save anime --crf 24`).

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | The run completed and every requested task succeeded |
| `1` | Any failure: a usage error, an invalid flag value, an unreachable ffmpeg, a failed task, or a prompt the user declined |
| `130` | The run was cancelled by a stop request (Ctrl-C, a signal, or a terminal prompt abort) |

No other exit code is part of the contract.

## `templates/<name>.json`

Location: the `templates` directory beside `config.json` (`%LOCALAPPDATA%\encro\templates\` on Windows, `~/.config/encro/templates/` elsewhere), so `ENCRO_CONFIG` relocates both. One file per template, named for the template (`<name>.json`); a name is lowercase letters, digits, `.`, `_` and `-`, starting with a letter or digit, and is matched case-insensitively.

A template file has the same shape as `config.json`: a JSON object keyed by the token names of the task-shape options, written in the same stable order, with native JSON types and one key per line. It always carries `"$schema": "./templates.schema.json"`, a reserved key the tool ignores. A key outside the templateable set is ignored with a warning and preserved by the next rewrite; a malformed file is an error naming it. `save` and `unset` rewrite the whole file canonically.

The templateable keys are the default-mode options except the input paths (`-i`, `-I`, positional inputs), `--state-file`, `-v`/`--verbose` and `-T`/`--template`. Booleans are stored as JSON booleans and cleared per run through their negation form (`--no-keep`, `--no-dry-run`, ...).

`templates.schema.json`, written by `save` beside the templates and printed by `encro template schema`, describes exactly those keys: JSON type, legal values or numeric bounds, built-in default and description. It is generated from the binary's own option table, never fetched from the network, and ships inside the binary rather than as a release asset.

A run's effective value for a configurable option is the command line first, then the selected template, then `config.json`, then the built-in default. Template values are defaults rather than parsed arguments, so a stored value never trips a flag's mutual-exclusion rule.

## `config.json`

Location: `%LOCALAPPDATA%\encro\config.json` on Windows, `~/.config/encro/config.json` elsewhere (`$XDG_CONFIG_HOME` when set), overridable with `ENCRO_CONFIG`. The file is a JSON object whose keys are the token names below. Keys are written in this order so the file stays diff-stable; unknown keys are rejected, and a malformed file is an error rather than silently ignored.

| Key | JSON type | Accepted values | Option it backs |
| --- | --- | --- | --- |
| `color` | string | `auto`, `always`, `never` | `--color` |
| `yes` | boolean | | `--yes` |
| `output-format` | string | `mp4`, `webp` | `--output-format` |
| `keep` | boolean | | `--keep` |
| `force-conflict-handling` | string | `y`, `n` | `--force-conflict-handling` |
| `folder-summary` | boolean | | `--folder-summary` |
| `recursive` | boolean | | `--recursive` |
| `jobs` | number | integer `>= 1` | `--jobs` |
| `ffmpeg-path` | string | directory path | `--ffmpeg-path` |
| `compress` | boolean | | `--compress` |
| `image-quality` | number | integer `2`–`31` | `--image-quality` |
| `crf` | number | integer `0`–`51` | `--crf` |
| `min-vmaf` | number | integer `0`–`100` | `--min-vmaf` |
| `preset` | string | `p1`–`p7`, `auto` | `--preset` |
| `video-codec` | string | `hevc_nvenc`, `libx265`, `libx264` | `--video-codec` |
| `pack` | boolean | | `--pack` |
| `model-dir` | string | directory path | `--model-dir` |
| `identity-tau` | number | `0`–`1` (fractional) | `--identity-tau` |

## `job-state.json`

Default location: `<work-root>/.encro/job-state.json`, where the work root is the deepest common ancestor of the run's inputs, or the path given to `--state-file`. `batch` keeps one state file per first-level subfolder and accepts no `--state-file`.

The file is a JSON object. Readers tolerate absent optional fields (they fall back to defaults) and older field layouts; writers emit the shape below.

| Field | Type | Meaning |
| --- | --- | --- |
| `version` | number | Schema version |
| `jobId` | string | Identifier of the run that owns this state |
| `stage` | string | Current stage, e.g. `planning` |
| `cancelRequested` | boolean | Whether a stop was requested while the run was live |
| `updatedAtMs` | number | Epoch milliseconds of the last write |
| `config` | object | Snapshot of the run's resolved settings |
| `tasks` | array | One record per unit of work |

`config` keys: `processType`, `outputFormat`, `outputLayout`, `packOutput`, `packOnly`, `recursive`, `forceNameConflictHandling`, `pictureFolderSummary`, `videoWebp`, `inputPaths` (string array), `outputPath` (string), optional `packOutputDir` (string), optional `encodeSettings` (object with `crf`, `videoCodec`, `nvencPreset`, `minVmaf`).

Each `tasks[]` record has: `id`, `kind`, `status`, `label`, `attemptCount`, `fingerprint`, `sourcePaths` (string array), `targetPaths` (string array), and the nullable progress fields `lastProgress`, `lastFrameCount`, `lastStatus`, `lastError`, `startedAtMs`, `updatedAtMs`, `finishedAtMs`, `segmentIndex`, `resumeTimeUs`, `encodedMs`.

## `--log-json` records

With `--log-json`, each log record is one JSON object on its own line (NDJSON). Every record carries these fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `timestamp` | string | RFC 3339 UTC with milliseconds, `YYYY-MM-DDTHH:MM:SS.sssZ` |
| `level` | string | Log level name |
| `module` | string | Emitting module |
| `source` | string | `filename:line`, empty when unknown |
| `message` | string | The human-readable message |
| `run_id` | string | Stable for the whole run, present on every record |

Optional fields, present when applicable: `error_context` (array of context frames, innermost last), `summary` (object, on the end-of-run record only), `elapsed_ms` (number, on timed scopes), and attributes stamped by the running task — currently `task_id` and `input`. Attributes never override a field already present.

## What this declaration does not cover

The following are deliberately outside the API and may change in any release without a version increment:

- Internal C++ interfaces, class and function names, and header layout.
- The test suite, its fixtures, fake tools and `xmake` task names.
- The exact wording of error, warning and progress text, and every rendering decision: bar glyphs, colors, column layout, whether a frame is drawn when stdout is not a terminal. This includes the wording and format of the `--version` line itself — only the version value in it is a promise.
- Files a run newly writes into the user's output directory, and the organization `organize` produces — a later version may create new files, but nothing it previously promised is removed or renamed.
- Cache and scratch formats under `.encro/`, `.encro/segments/`, `.encro/webp/` and the probe cache, which are rebuildable by re-running.
- The human-readable log file format.

## From a change to an increment

| Surface touched | Increment |
| --- | --- |
| A subcommand, flag, or positional argument removed or renamed | MAJOR |
| A flag's accepted value set narrowed, or its meaning replaced | MAJOR |
| An exit code changed | MAJOR |
| A `config.json` key removed, renamed, or changed in type | MAJOR |
| The default job-state path moved, or its field names or types changed incompatibly | MAJOR |
| An NDJSON field removed or renamed | MAJOR |
| A new subcommand, flag, config key, state field or NDJSON field, or a widened value set | MINOR |
| A new templateable key, a new schema field, or a change to the template file's shape | MINOR |
| A behavior fix with no change to the surfaces above | PATCH |

Incompatible changes accumulate under a single MAJOR as long as no version carrying them has been published; a published version's tag is never re-pointed.
