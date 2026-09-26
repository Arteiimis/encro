## Context

See `proposal.md` — Why for the three duplicated mechanics and the copies that already drifted. This section records only what shapes the approach.

Constraints:

- C++26, `clang-cl` + `lld-link`, built with xmake (`xmake.lua:5`).
- `picture_compress.h` is picture-only and already declares `partialTempPath` (`:25`) with a stated contract: the temp path keeps the target media extension so the encoder infers the container (`:22-24`). `picture_video_webp.cpp` already includes it (`:9`) and reuses the rule (`:63`, `:121`), so a finalize helper belongs beside it and needs no new dependency.
- `core/progress.h` already owns `showsSlotBars` (`:36`) and already carries `<vector>`, `<string>`, `<string_view>` (`:9-18`); `progress.cpp` already includes `<format>`. A slot-bar builder added there costs its includers no new include — the archive change had to keep its planner out of `collision_naming.h` for exactly that reason. The builder takes a `ProgressContext&`, so its declaration goes after the class (`:84`), beside `fitPostfixText` (`:172`). This mattered only to the reverted builder (`D3`); the shipped change leaves `progress.{h,cpp}` untouched.
- The three staging callers are not one function with one varying parameter. Verified differences:

| | `job_state::detail::flushSnapshot` (`job_state.cpp:470`) | `organize::AnalysisCache::saveLocked` (`cache.cpp:168`) | `probecache::save` (`probe_cache.cpp:145`) |
| --- | --- | --- | --- |
| temp name | `makeTempStatePath` → `{name}.tmp` (`:58-60`) | `filePath + ".tmp"` (`:179`) | `path + "." + pid + ".tmp"` (`:175-176`) |
| open mode | `std::ios::trunc` (`:482`) | `std::ios::binary` (`:181`) | default (`:178`) |
| throttle | 2 s + force (`:27`, `:477`) | 64-put batch (`cache.h:30`) | none |
| directory creation | no | `fs::create_directories` (`:177`) | no |
| failure channel | `eh::Result`, two messages (`:483-493`) | silent `return` (`:182`, `:185`) | `LOG_WARN` on open (`:180`); write unchecked |
| rename | once, then remove-and-retry (`:497-502`) | once (`:187`), error ignored | once (`:206`), `LOG_WARN` on error (`:207`) |
| read-merge-write | no | no | yes (`:146-170`) |

- The two finalize sites differ only in policy: `compressImage` checks existence earlier with its own warning (`picture_compress.cpp:75-82`) and names `input=` and `output=` in its rename warning (`:88-93`); `finalizeConvertedOutput` checks existence itself and returns `false` silently (`picture_video_webp.cpp:64`), naming only `output=` in its warning (`:70-74`).
- Behaviour preservation is judged by the existing suites, unmodified: `tests/picture/picture_compress_tests.cpp` (`[picture]`, `[picture-compress]`), `tests/picture/picture_video_webp_tests.cpp` (`[picture-process]`, `[video-webp]`), `tests/job_state_tests.cpp` (`[job-state]`), `tests/organize/stage_tests.cpp` (`[organize]`), `tests/video/probe_cache_tests.cpp` (`[probe-cache]`), `tests/video/video_batch_execution_tests.cpp` (`[video-batch-execution]`), `tests/video/encode_probe_tests.cpp` (`[encode-probe]`), `tests/infra/progress_tests.cpp` (`[progress]`).

## Goals / Non-Goals

**Goals:**

- One implementation each of "remove the target, rename the partial over it" and "open the caller's staging path, write, flush, close, report". The broader goal — one implementation of the per-slot bar builder — was measured and reverted; see `D3`.
- Every caller keeps what makes it different: temp name, open mode, throttle, error channel and wording, directory creation, rename policy, and the bar label.
- No existing test file is edited and no exercised console output moves — that is the correctness evidence; the one new case (`tests/file_write_tests.cpp`) only pins `writeStagingFile`'s own contract.

**Non-Goals:**

- One generic temp-path rule (D2: the suffixes are load-bearing).
- Unifying throttles, error channels, directory creation or rename policies (D2).
- Unifying the extension lists or fixing the uppercase `.MP4` skip (D4: recorded, not fixed).
- Extracting the per-slot bar builder — implemented, measured at +11 source lines, reverted (D3).
- Everything already listed as out of scope in `proposal.md`, each for its own reason: the packer scan must see every file; the confirm prompts are already centralized in `readUserIpt`; `CompactProgressState` is a progress context, not a writer; `copySafely` copies a whole file through its own `.part` staging, which the string-content contract cannot express; the model download fetches a binary through curl and verifies a checksum; the two collision loops answer different naming questions (hash-suffixed zip entry name vs `stem_<n>`); the non-atomic direct writers have no staging path.

## Decisions

### D1: `finalizePartialOutput` sits beside `partialTempPath`; policy stays at the callers

Add `auto finalizePartialOutput(fs::path const& outputPath) -> std::error_code` next to `partialTempPath`: declaration in `src/picture/picture_compress.h:25`, definition in `src/picture/picture_compress.cpp:38-41`. It computes `partialTempPath(outputPath)`, removes the target, renames the partial over it, and returns the rename's error code. The remove's error code is discarded, which is what both sites do today (the rename overwrites it).

The helper owns no policy: no existence check, no logging, no success/failure decision.

- `compressImage` replaces `:84-95` with the call and keeps its `fs::exists(partialPath)` guard and warning (`:75-82`), its `"Image compression output rename failed: input=... output=... error=..."` text (`:88-93`) and its `false` return.
- `finalizeConvertedOutput` replaces `:66-76` with the call and keeps its own `fs::exists(tempPath)` guard (`:64`), its `"Video conversion output rename failed: output=... error=..."` text (`:70-74`) and its `true`/`false` returns.

Alternatives considered:

- **Take both paths as parameters** — `partialTempPath` is a pure function of the output path; a second parameter would let a caller pass a mismatched pair.
- **Fold the existence check in** — the conversion site's guard returns `false` silently, while the compression site already warns earlier with a different message; one shared guard would either silence or duplicate a warning.
- **Log inside the helper** — the two messages differ in which paths they name, so logging would move and flatten them.
- **Return `bool`** — both callers need `ec.message()`.
- **Put it in `core`** — the rule it wraps is picture-specific; `core` has no business knowing `partialTempPath`.

### D2: `fileio::writeStagingFile` shares only the staging body, and the temp names stay three rules

Add header-only `src/core/file_write.h`:

```cpp
namespace fileio {

enum class StagingStatus { Written, OpenFailed, WriteFailed };

auto writeStagingFile(
  fs::path const& stagingPath,
  std::string_view content,
  std::ios::openmode mode = std::ios::out
) -> StagingStatus;

}  // namespace fileio
```

Body: open `std::ofstream{stagingPath, mode}` (failure → `OpenFailed`); `out << content`; `out.flush()` (either failure → `WriteFailed`); `out.close()` (unchecked, as all three sites leave it today); `Written`. Closing before return preserves today's "closed before rename" — explicit at `job_state.cpp:494`, block-scope at `cache.cpp:180-186` and `probe_cache.cpp:177-203` — which matters on Windows.

Why an enum rather than `eh::Result`: the helper must not impose an error channel. `eh::Result` would force job-state's wording and failure policy onto a caller that is silent and one that only warns. Why `std::string_view`: all three contents are contiguous strings — `json::serialize` temporaries (`job_state.cpp:489`), a local `std::string` (`cache.cpp:173-174`), a `boost::json::serialize` temporary (`probe_cache.cpp:197-202`) — and it binds to them for the call's lifetime without a copy. Why `std::ios::openmode`: each caller passes exactly the flags it passes today, so the helper adds no `trunc` and drops none. Because the content is a call argument, `flushSnapshot` (`job_state.cpp:482-489`) and `probecache::save` (`probe_cache.cpp:178-182`) now serialize before the open is attempted where they open first today; both builds are pure and the same error line prints on failure, and `saveLocked` already built its content before opening (`cache.cpp:170-181`). The proposal therefore claims no *output* change, not no reordering.

Per-caller mapping, unchanged behaviour:

| | `OpenFailed` | `WriteFailed` | rename |
| --- | --- | --- | --- |
| `flushSnapshot` | error `"Failed to open state temp file for writing: {}"` | error `"Failed to write state snapshot to: {}"` | stays at the caller (`:497-502`, remove-and-retry) |
| `saveLocked` | silent `return` | silent `return` | stays (`:187`, error ignored) |
| `probecache::save` | `LOG_WARN` + `return` (`:180`) | ignored — the rename still happens, exactly as today | stays (`:206-207`, `LOG_WARN` on error) |

The probe-cache write check is the one place the helper's report is deliberately dropped. Today that copy never checks the write, so a short write renames over the cache; the helper makes that visible at the call site instead of hiding it inside a copy, and this change preserves it (the comment at the call site points here). Fixing it would change console output on a failure path and belongs in its own change.

**The suffixes are load-bearing; a single generic temp-path helper would be wrong.** The rules encode four different facts:

- `.partial` (picture, `partialTempPath`) keeps the target *media extension* — `<stem>.partial.<ext>` — because the encoder infers the container from the extension; a generic `.tmp` name would make the encoder see an unknown format. The contract is written at `picture_compress.h:22-24` and both picture paths depend on it (`picture_compress.cpp:50`, `picture_video_webp.cpp:63`, `:121`).
- `.tmp` marks JSON state (`job_state.cpp:58-60`, `cache.cpp:179`): a name that says "not an input, not a media file", kept in the target's directory for a same-volume rename.
- `.part` marks a byte copy of a file whose final name matters (`model_store.cpp:49`, `:159`; `organize/execute.cpp:23-24`) — the same convention as the picture partial, for the same reason.
- `probe_cache`'s `.<pid>.tmp` keeps two concurrent processes from staging under one name (`probe_cache.cpp:172-176`).

A shared "one true temp name" helper would erase the media-extension contract, the JSON/copy distinction and the pid uniqueness at once. The staging helper therefore takes the caller's path; it never builds one.

Alternatives considered:

- **Move the whole write-then-rename into the helper** (final path + rename policy) — the renames differ (remove-and-retry vs silent vs warn) and probe-cache's read-merge-write and eviction (`:146-170`) must stay; a policy parameter would be a switch in disguise.
- **Fold the throttles in** — 2 s force-able vs 64-put batch vs none; they are caller schedules, not write mechanics.
- **Return `eh::Result`** — see above.

### D3: The slot-bar builder is not extracted — measured, then reverted

The first pass moved the per-slot bar loop into `progress::makeSlotBars` and the `"<label>: [idle-<n>]"` format into `progress::slotBarIdleText`, called from `EncodingProgressState` (`src/video/video_batch_execution.h:92`, private `makeSlotBars` `:115-129` deleted, `barIdle`'s postfix `:224` routed through the formatter) and from `createProbeBars` (`src/video/encode_probe.cpp:560-562`, `initSlotBars` `:536-545` deleted, the caller's own `if (showsSlotBars(...))` with it).

The review measured it rather than arguing about it, from `git diff --numstat`:

| | added | removed |
| --- | --- | --- |
| `src/core/progress.{h,cpp}` | +38 | 0 |
| `video_batch_execution.h` call site | +3 | −18 |
| `encode_probe.cpp` call site | +2 | −14 |
| **total** | **+43** | **−32** |

`progress.{h,cpp}` gained 38 lines while the two call sites shed 27 net (32 deleted, 5 added): source net +11. What is actually shared is a one-line `[idle-N]` format plus a one-line `showsSlotBars` gate. This design's own drop-first paragraph had pre-authorised exactly this outcome — "If implementation or review shows the change is too large, delete section 3 and keep D1 and D2" — and `reuse-hash-and-naming-helpers` D3 is the precedent for a measured fallback.

What replaced it: nothing. The two builders and `barIdle`'s postfix are restored to their pre-change bodies, `src/core/progress.{h,cpp}` is byte-identical to the pre-change tree (`git diff 69c8814 -- src/core/progress.h src/core/progress.cpp` is empty), and the three `[idle-...]` writers stay. The bar-text check the change had planned was a hole anyway: task 3.4's greps pinned *where* the idle text lives, never the text, so `slot + 1` → `slot` would have kept every grep green while printing `[idle-0]` instead of `[idle-1]`, contradicting `openspec/specs/terminal-color-palette/spec.md`. With the code byte-identical to its pre-change state, that hole closes by construction — there is no new idle-text rule left to pin, and a check that cannot fail would be worse than no check.

Alternative considered: keep `makeSlotBars` but drop `slotBarIdleText` and inline the format at the two builders and `barIdle` — rejected, it leaves the format three times over and still pays the +38 for the one-line gate, which is the only thing the extraction was actually worth.

### D4: The extension lists and the uppercase `.MP4` skip are recorded in `docs/backlog.md`, not fixed

One backlog entry, nothing else:

- `organize::kImageExtensions` (`src/organize/scan.h:20-29`) — 8 entries, uppercase variants listed deliberately because `media::scanByExtensions` is case-sensitive (`scan.h:18-19`).
- `readAllPics`'s `pictureTypes` (`src/picture/picture_process.cpp:802-811`) — 7 entries, no `.webp`, adds `.bmp`/`.tiff`/`.gif`/`.heic`.
- `videoinfo::kVideoTypes` (`src/video/video_info.cpp:31-38`) — 6 lowercase entries.
- `pack::kStoredMediaExtensions` (`src/pack/pack_types.h:22-53`) — 27 entries, a different question (STORE vs deflate) and already case-insensitive through `shouldStoreEntry` (`:55-61`).

The silent skip: `isKnownVideoExtension` (`video_info.cpp:110-115`) and `media::extensionMatches` (`media_scanner.cpp:18-24`), the comparison `media::scanByExtensions` uses at `:44` and `:94`, both compare case-sensitively against the lowercase list, so an uppercase `.MP4` never enters either video scan (`video_info.cpp:300`, `:571`) nor the picture run's conversion scan.

Unifying them changes which files each command sees, which is a spec change, not a dedup; the diagnosis is cheap to record and expensive to re-derive. Alternatives: unify on the organize set (changes picture/video file sets); make matching case-insensitive (a superset for every command, needs a spec change); do nothing (the skip stays invisible).

## Risks / Trade-offs

- **Behaviour preservation rests on unmodified tests.** If any of the eight suites above needs an edit to pass, a helper changed behaviour — stop and re-derive that decision. The change edits no existing test file by design; `tests/file_write_tests.cpp` is new and only pins the `writeStagingFile` contract.
- **The suffix trap.** A later reader may "simplify" the three staging callers into one temp-path helper; D2 records why that is wrong (media-extension inference, JSON vs copy, pid uniqueness). The call sites keep their own names on purpose.
- **The staging helper can be read as license to unify policy.** Throttles, error channels, directory creation and rename stay per caller; D2's mapping table is the contract, and the tasks verify the three temp rules and throttles survive.
- **`probecache::save` still ignores a write failure** (`WriteFailed` dropped, rename proceeds) — a pre-existing hole the helper makes visible but does not fix, to keep console output identical on that path. If review wants it fixed, it is a separate change.
- **One helper has a direct test; the other's error branch does not.** `fileio::writeStagingFile` is pinned by one case in the new `tests/file_write_tests.cpp` (`Written` with the content landed; `OpenFailed`; `WriteFailed` has no portable trigger and the case records it as uncovered). `finalizePartialOutput` has no direct case — its error branch is exercised only through the callers' suites, and its regression is the two warning strings, which the change leaves unchanged. The acceptance bar is "no *existing* test file edited", the new case adds coverage only, and `tests.exe -r console -s` is the extra probe that no output moved.
- **D3 fired its drop-first fallback.** The extraction was implemented, measured at +11 source lines, and reverted (`D3`); D1 and D2 stand alone and `src/core/progress.{h,cpp}` is untouched by the change.

## Migration Plan

Each step is one commit and independent; no persistent file format or CLI surface changes, so a rollback is `git revert` of that step's commit.

1. Add `finalizePartialOutput` and adopt it at both picture sites; run `xmake test-report --tag="[picture]"`, `--tag="[picture-compress]"`, `--tag="[video-webp]"`, `--tag="[picture-process]"`.
2. Add `src/core/file_write.h` and adopt it at the three staging sites; run `--tag="[job-state]"`, `--tag="[organize]"`, `--tag="[probe-cache]"`.
3. Add `progress::makeSlotBars` and adopt it at both bar sites; run `--tag="[video-batch-execution]"`, `--tag="[encode-probe]"`, `--tag="[progress]"`. **This step was reverted** — the code-stage review measured it at +11 source lines (`D3`), and `progress.{h,cpp}`, `video_batch_execution.h` and `encode_probe.cpp` are back to their pre-change bodies.
4. Append the extension-list/`.MP4` entry to `docs/backlog.md`; verify only that file changed.
5. Full battery before commit: `xmake test-report`, `xmake build e2e_tests && xmake run e2e_tests`, `xmake test-parallel`, the reporter probe, `xmake fmt` idempotent, `xmake tidy` with no new diagnostics (`xmake tidy` is report-only, so compare the pre-change count by hand). Planning artifacts go in their own `docs:` commit first; implementation lands in one `refactor:` commit.

## Open Questions

None. The one judgement call that could have been deferred — whether `probecache::save` should start reporting write failures — is decided in D2 as "no, preserve". The pre-authorised D3 fallback is no longer open either: it fired (`D3`).
