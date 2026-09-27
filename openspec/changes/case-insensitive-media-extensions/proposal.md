## Why

Every media extension list in the program holds lowercase spellings, but the comparisons that match a file against those lists are case-sensitive: `media::extensionMatches` (`src/core/media_scanner.cpp:18-24`, used by `scanByExtensions` at `:44` and `:94`) and `videoinfo::isKnownVideoExtension` (`src/video/video_info.cpp:110-115`). So `CLIP.MP4` never reaches a video scan, `PHOTO.JPG` never reaches the picture scan, and the collection reads as "no input found" or is partially processed with nothing naming the skip. The lists also disagree with one another about membership; that disagreement is a separate question and is not settled here.

## What Changes

- Extension matching becomes ASCII case-insensitive everywhere a media extension is matched, through one shared comparison: the scanner's matcher and the video command's known-extension predicate.
- `videoinfo::isKnownVideoExtension` reuses the scanner's matcher instead of keeping a second, case-sensitive `std::ranges::contains` against its own list.
- `organize::kImageExtensions` drops its explicit uppercase duplicates — `.JPG`, `.JPEG`, `.PNG`, `.WEBP` now match through the rule — and its comment claiming case-sensitive matching.
- Per-command extension lists keep their membership and their owners: organize keeps `.jpg/.jpeg/.png/.webp`, the picture run keeps its seven types, the video workflow keeps its six. The membership disagreement is untouched.
- The rule is a strict superset of today's behavior: it can only add files that are silently skipped now, and no file processed today stops being processed.
- No breaking change: no CLI flag, config key, persisted state or list membership changes.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `media-scan`: gains the case rule — media extension matching is an ASCII case fold, independent of the process locale.
- `image-character-organize`: its scan accepts uppercase spellings of its image extensions.
- `picture-video-webp`: the video scan accepts uppercase spellings of the video extensions.

## Impact

- **Code**: `src/core/media_scanner.{h,cpp}` (the comparison and its declaration), `src/organize/scan.h` (duplicate entries and stale comment), `src/video/video_info.cpp` (predicate re-pointed at the shared matcher). `src/picture/picture_process.cpp`'s list is unchanged: it inherits the rule through `scanByExtensions`.
- **Tests**: `tests/media_scanner_tests.cpp` (a `[media-scanner]` case for the rule), `tests/video_info_tests.cpp` (a flow-level `[video-info]` case for an uppercase-extension directory and single-file input). Existing cases already pin the lists: `tests/organize/port_tests.cpp:32` scans `b.JPG`, `tests/video_info_tests.cpp:418` filters unknown extensions.
- **Specs**: the three delta specs above. `docs/backlog.md`'s "Extension lists disagree, and an uppercase `.MP4` never reaches a video scan" is the origin and is closed when this change archives; it is not edited here.
- **Dependencies**: none.
