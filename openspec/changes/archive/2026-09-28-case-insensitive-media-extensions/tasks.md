## 1. Red-first tests

- [x] 1.1 Add the failing case in `tests/media_scanner_tests.cpp` (`[media-scanner]`): a directory holding `CLIP.MP4` and `PHOTO.Jpg` is scanned with the lowercase set {`.mp4`, `.jpg`} and both files are reported as matches, and the same rule holds for a single-file root (`scanByExtensions(mixedFile, std::array{".jpg"sv}, false)` reports the file); verify with `xmake test-report --tag="[media-scanner]"` that it fails for the current behavior (`std::ranges::contains` at `src/core/media_scanner.cpp:23` compares exactly).
- [x] 1.2 Add the failing flow-level case in `tests/video_info_tests.cpp` (`[video-info]`): `readAllVids` over a directory holding `CLIP.MP4` reports that clip, and `readAllVidsFromFiles` with the `CLIP.MP4` path keeps it — the two halves cover the scanner comparison and `isKnownVideoExtension` respectively; verify with `xmake test-report --tag="[video-info]"` that the directory half fails through the scanner and the single-file half fails through `isKnownVideoExtension` (`src/video/video_info.cpp:114`).

## 2. The case rule

- [x] 2.1 Declare `media::extensionMatches(fs::path const&, std::span<std::string_view const>)` in `src/core/media_scanner.h` with a comment stating the ASCII case-insensitive rule, move the definition out of the anonymous namespace in `src/core/media_scanner.cpp`, and implement the comparison with a `constexpr` ASCII fold (A–Z to a–z) plus `std::ranges::any_of` over the set — no `std::tolower`, no locale; verify cases 1.1 and 1.2's directory half pass and no other `[media-scanner]` case changes.
- [x] 2.2 Replace `isKnownVideoExtension`'s body (`src/video/video_info.cpp:110-115`) with a call to `media::extensionMatches(filePath, kVideoTypes)` and drop the now-unused function-local `namespace rng = std::ranges;` (the `<algorithm>` include stays: `std::min`/`std::max` at `:208`, `:263`, `:265` still need it); verify case 1.2's single-file half passes and `xmake test-report --tag="[video-info]"` is green.
- [x] 2.3 Delete the four uppercase duplicates from `organize::kImageExtensions` (`src/organize/scan.h:20-29`) and rewrite the comment that claims `media::scanByExtensions` matches case-sensitively, keeping the membership `.jpg/.jpeg/.png/.webp`; verify `tests/organize/port_tests.cpp:32`'s existing `b.JPG` case still passes with `xmake test-report --tag="[organize]"`.
- [x] 2.4 Confirm the untouched lists still behave: `src/picture/picture_process.cpp:802-811`'s seven picture types and `src/video/video_info.cpp:31-38`'s six video types keep their membership and reach the shared rule through `scanByExtensions`; verify with `xmake test-report --tag="[picture-process]"` and `--tag="[video-process]"`.

## 3. Verification

- [x] 3.1 Run `xmake fmt` twice and confirm the second run leaves no diff; run `xmake tidy` and compare its diagnostic count against the baseline taken before the implementation (around 133 warnings), with no new diagnostic in the changed files.
- [x] 3.2 Run the full `xmake test-report` with zero failures and record the assertion/case counts; run the filtered tags `[media-scanner]`, `[organize]`, `[picture-process]`, `[video-process]` and confirm each is green.

## 4. Planning-artifact review (before implementation)

- [x] 4.1 Run the planning-artifact stage of the `code-review` skill (one fresh reviewer, Coherence then Ground truth, findings quoting both sides) against proposal, delta specs, design and tasks; record every finding and its verdict (`resolved (<commit>)` / `rejected: <reason>`) here before the implementation commit.

### Planning-artifact review findings (task 4.1)

One fresh reviewer ran the planning-artifact stage (Coherence, then Ground truth) against the artifacts at `5c630ed`, resolving citations against that commit because the working tree already carried the implementation. Five findings:

1. [medium][Coherence] The `picture-video-webp` scenario "Uppercase video extension is scanned" (`specs/picture-video-webp/spec.md`: "**WHEN** the input holds a clip named `CLIP.MP4` and the run enables conversion") is implemented by no task: task 1.2 covers `readAllVids`/`readAllVidsFromFiles` and task 2.4 only reruns existing tags. — **rejected: the picture run's conversion scan reaches the same shared matcher (`src/video/video_info.cpp:294` -> `media::scanByExtensions`, caller `src/picture/picture_process.cpp:834`); the rule is pinned at the cheapest level by the task 1.1 `[media-scanner]` case, and the flow by the existing `[picture-process][video-webp]` case (`tests/picture/picture_video_webp_tests.cpp:163`), so a duplicate uppercase case would re-assert what a unit test already covers.**
2. [medium][Ground truth] proposal "Every media extension list in the program holds lowercase spellings" vs `5c630ed:src/organize/scan.h:25` `std::string_view{".JPG"},` (and `.JPEG`/`.PNG`/`.WEBP` at `:26-28`); design repeated it ("Every list holds lowercase spellings."). — **resolved (43e7b25)**: both now say every other list holds lowercase spellings only.
3. [low][Ground truth] design "the video workflow (`src/video/video_info.cpp:300`, `:571`)" vs `5c630ed:src/video/video_info.cpp:291` "// Probe-free, size-gated video scan for the picture run's WebP conversion." — **resolved (43e7b25)**: `:300` is now named as the picture run's conversion scan.
4. [low][Ground truth] tasks 1.1 sketch `scanByExtensions(mixedFile, {".jpg"}, false)` vs `5c630ed:src/core/media_scanner.h:27` `std::span<std::string_view const> extensions` — `std::span` has no initializer-list constructor, so the sketch cannot compile. — **resolved (43e7b25)**: the sketch now uses the `std::array{".jpg"sv}` form the tests use.
5. [low][Ground truth] `videoinfo::isKnownVideoExtension` (proposal, design, tasks) vs `5c630ed:src/video/video_info.cpp:26`/`:286` — the helper sits in the file's anonymous namespace, with only the internal caller at `:153`. — **resolved (43e7b25)**: the qualifier is dropped in all four places.

The reviewer also listed what it could not check: the tidy baseline count (recorded in this session's report, not in the repo) and the red/green transitions of tasks 1.1-1.2 (the tree already carried the implementation when it ran).

## 5. Code-diff review (before the implementation commit)

- [x] 5.1 Run the code-diff stage of the `code-review` skill on the implementation diff (Standards, Spec, Leanness as parallel reviewers) and record the findings, verdicts and fix loop here before the implementation commit lands.

### Code-diff review findings (task 5.1)

Three fresh reviewers ran the code-diff stage in parallel (Standards, Spec, Leanness) against `git diff 5c630ed -- src tests`; a fresh verifier then re-read the tree and the diff and returned a verdict per finding. All 15 findings resolved or soundly rejected:

**Standards**

- [hard] Trailing return for scalars: `auto extensionMatches(...) -> bool` and `constexpr auto asciiLower(char) -> char` violate AGENTS.md's "Prefix style (`bool f()`) for scalars and `void`". — **resolved**: `bool extensionMatches(...)`, `constexpr char asciiLower(char ch)`.
- [judgement] The header and the .cpp repeat the same three facts ("Comments: Minimal"). — **rejected: the header states the matching contract; the .cpp comment documents the fold helper's semantics (design D2's no-locale decision), a different symbol.**
- [judgement] One `[media-scanner]` case bundled two spec scenarios. — **resolved**: split into "matches uppercase and mixed-case extensions in a directory" and "matches an uppercase extension for a single-file root".
- [judgement] Nothing calls `extensionMatches` directly; a direct case would be cheaper than temp files. — **rejected: the requirement's contract is scan-level ("The rule SHALL apply to every scan"), and the single-file-root case exercises the matcher through the public scan API at the cheapest scan level.**
- [judgement] The exactness clause ("SHALL remain exact on every other character") had no regression probe. — **resolved**: added `scanByExtensions does not fold letters outside A-Z` (`a.{` against `.["`); confirmed red by temporarily replacing the fold with `ch | 0x20` (0x5B -> 0x7B) and green after restoring it.

**Spec**

- pack's `pack::shouldStoreEntry` folds with locale-dependent `std::tolower`, so the media-scan "SHALL NOT depend on the process locale" sentence reads wider than the implementation. — **rejected: `src/pack/` is another workstream's (design Non-Goals) and the requirement is scoped to scans; `git diff 5c630ed -- src/pack` is empty.** Flagged for the pack owner in the change report.
- picture-video-webp's `CLIP.MP4` scenario is not driven end to end. — **rejected: same reason as planning finding 1.**
- No test pins non-ASCII exactness / locale independence. — **resolved in part: the A-Z-only probe pins the fold's extent, which is D2's locale-independence mechanism; a non-ASCII filename probe would depend on `fs::path::string()`'s code-page conversion, the variable under test, so it is deliberately not asserted.** Residual: nothing goes red if `asciiLower` became `std::tolower` under a non-C locale.
- Scope creep: none. Implemented-but-wrong: none.

**Leanness**

- `shrink:` the nested predicate lambda re-implemented the defaulted comparison. — **resolved**: `std::ranges::equal(candidate, ext, std::ranges::equal_to{}, asciiLower, asciiLower)` (plus the `<functional>` include).
- `stdlib:` `std::ranges::find(results, x) != results.end()` in the new tests. — **resolved**: `std::ranges::contains`.
- `net: -2 lines possible.` — applied.

Verification: the fresh verifier ran the five affected tags (3044 assertions / 160 cases green) and returned resolved or sound-rejected for every finding above.
