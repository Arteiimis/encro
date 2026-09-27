## Context

See `proposal.md` — Why. The state that shapes the approach:

- Two comparisons match a file against a media extension list, and neither folds case:
  - `media::extensionMatches` (`src/core/media_scanner.cpp:18-24`) takes `filePath.extension().string()` and calls `std::ranges::contains` (`:23`) over the caller's set. It is the only comparison `scanByExtensions` uses, for a directory walk (`:44`) and for a single-file root (`:94`), so every command's scan inherits its behavior: organize (`src/organize/scan.cpp:30`), the picture run (`src/picture/picture_process.cpp:813`) and the video workflow (`src/video/video_info.cpp:300`, `:571`).
  - `videoinfo::isKnownVideoExtension` (`src/video/video_info.cpp:110-115`) does its own `std::ranges::contains(kVideoTypes, vidsExt)` (`:114`) for the video command's explicit file input (`tryCollectVideoInput`, `:153`), which never goes through the scanner.
- Every list holds lowercase spellings. organize's `kImageExtensions` (`src/organize/scan.h:20-29`) is the outlier: it holds eight entries because four uppercase duplicates were added by hand to work around the case-sensitive comparison, with a comment saying so (`:18-19`). The picture run's `pictureTypes` (`src/picture/picture_process.cpp:802-811`, seven entries) and the video workflow's `kVideoTypes` (`src/video/video_info.cpp:31-38`, six entries) hold lowercase only, so their uppercase spellings are silently skipped.
- The pack module already matches its STORE/deflate extension whitelist case-insensitively (`pack::shouldStoreEntry`, `src/pack/pack_types.h:55-61`), which is the precedent for the rule but not a reusable implementation: `src/pack/` is owned by another workstream and stays untouched.
- Test seams that already exist: `tests/media_scanner_tests.cpp` drives `scanByExtensions` directly, `tests/video_info_tests.cpp` drives `readAllVids`/`readAllVidsFromFiles` (`:418` pins the unknown-extension filter), and `tests/organize/port_tests.cpp:32` already scans a `b.JPG` through organize.

## Goals / Non-Goals

**Goals:**

- One case rule, stated once in `media-scan` and implemented once, so no matcher can drift case-sensitive again.
- The rule is ASCII-only and locale-independent: the bytes in the filename decide, not the process environment.
- Behavior is a strict superset: only files silently skipped today can be added; nothing processed today is dropped.

**Non-Goals:**

- The per-command extension membership disagreement (organize's four image types, the picture run's seven with no `.webp`, the video workflow's six): each list keeps its membership, its owner and its file. Recorded in `docs/backlog.md` as a separate question.
- `src/pack/`: its whitelist answers a different question (STORE vs deflate) and is already case-insensitive; nothing there changes.
- Warning when a file is skipped for a genuinely unlisted extension: pre-existing behavior, not part of the case rule.
- Renaming or re-homing any list, and merging them into one canonical set: that is the membership question.

## Decisions

**D1 — One shared matcher, reused by both comparison sites.**
`media::extensionMatches` moves out of the anonymous namespace and is declared in `src/core/media_scanner.h`; `videoinfo::isKnownVideoExtension` becomes a call to it. Alternatives considered: (a) also fold case inside `video_info.cpp` — two copies of the rule that can drift apart again, which is how the defect arose; (b) add uppercase spellings to every list — does not cover mixed-case names such as `.Mp4`, multiplies entries, and organize's list is already the evidence of where that path leads. The scanner is the module that owns "which extensions match", so the rule lives there.

**D2 — The fold is a local ASCII fold, not `std::tolower`.**
`std::tolower` reads the process locale; whether `CLIP.MP4` is input must not depend on the environment a user or CI happens to run in. The matcher compares with a `constexpr` function that folds `A`–`Z` to `a`–`z` and leaves every other byte alone, so non-ASCII bytes compare exactly. The repo's other `std::tolower` uses (pack's `shouldStoreEntry`, collision naming, terminal output) are out of scope and keep their behavior.

**D3 — organize's duplicate uppercase entries are removed; its membership is unchanged.**
Under the fold, `.JPG`, `.JPEG`, `.PNG` and `.WEBP` match their lowercase entries, so the four duplicates are dead and the comment above them asserts a case-sensitivity that no longer holds. The set of extensions organize accepts — jpg, jpeg, png, webp — is unchanged. The picture and video lists are untouched: they were already lowercase-only, and the rule reaches them through the scanner.

**D4 — Each widened input set is recorded in its own capability, but the rule is stated once.**
`image-character-organize` and `picture-video-webp` each gain a scenario naming the uppercase spelling that is now scanned, so the widening is visible where the input set is owned; neither restates the fold mechanism, which `media-scan` owns. The picture run's own extension set is not named by any main spec; the general `media-scan` requirement covers it and no new capability is invented for it.

## Migration Plan

None: no persisted state, no configuration, no on-disk artifact, no CLI surface. Rollback is reverting the implementation commit.

## Risks / Trade-offs

- **Uppercase-spelled files enter existing commands' input sets** (a `CLIP.MP4` is now encoded, a `PHOTO.JPG` is now organized) → intended, and it is the change's whole point; it can only add files that were skipped silently, so no existing run loses work.
- **A file formerly skipped by mistake may now fail a later stage** (e.g. a bogus `CLIP.MP4` that is not a real video) → it now surfaces as that stage's ordinary per-file failure, which is the behavior the user asked for by naming an `.mp4` file; nothing is corrupted.
- **Case-insensitive filesystems (NTFS) hide the difference** → the rule still matters for the spellings carried in the directory entry and for case-sensitive filesystems; the tests copy real filenames, so they pin the rule on any platform.
- **Non-ASCII extensions are not folded** → deliberate: an ASCII case fold cannot be confused by code-page differences, and the lists are ASCII extensions.
