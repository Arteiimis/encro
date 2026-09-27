## 1. Red-first tests

- [ ] 1.1 Add the failing case in `tests/media_scanner_tests.cpp` (`[media-scanner]`): a directory holding `CLIP.MP4` and `PHOTO.Jpg` is scanned with the lowercase set {`.mp4`, `.jpg`} and both files are reported as matches, and the same rule holds for a single-file root (`scanByExtensions(mixedFile, {".jpg"}, false)` reports the file); verify with `xmake test-report --tag="[media-scanner]"` that it fails for the current behavior (`std::ranges::contains` at `src/core/media_scanner.cpp:23` compares exactly).
- [ ] 1.2 Add the failing flow-level case in `tests/video_info_tests.cpp` (`[video-info]`): `readAllVids` over a directory holding `CLIP.MP4` reports that clip, and `readAllVidsFromFiles` with the `CLIP.MP4` path keeps it — the two halves cover the scanner comparison and `isKnownVideoExtension` respectively; verify with `xmake test-report --tag="[video-info]"` that the directory half fails through the scanner and the single-file half fails through `isKnownVideoExtension` (`src/video/video_info.cpp:114`).

## 2. The case rule

- [ ] 2.1 Declare `media::extensionMatches(fs::path const&, std::span<std::string_view const>)` in `src/core/media_scanner.h` with a comment stating the ASCII case-insensitive rule, move the definition out of the anonymous namespace in `src/core/media_scanner.cpp`, and implement the comparison with a `constexpr` ASCII fold (A–Z to a–z) plus `std::ranges::any_of` over the set — no `std::tolower`, no locale; verify cases 1.1 and 1.2's directory half pass and no other `[media-scanner]` case changes.
- [ ] 2.2 Replace `videoinfo::isKnownVideoExtension`'s body (`src/video/video_info.cpp:110-115`) with a call to `media::extensionMatches(filePath, kVideoTypes)` and drop the now-unused function-local `namespace rng = std::ranges;` and the `<algorithm>` include if nothing else in the file needs it; verify case 1.2's single-file half passes and `xmake test-report --tag="[video-info]"` is green.
- [ ] 2.3 Delete the four uppercase duplicates from `organize::kImageExtensions` (`src/organize/scan.h:20-29`) and rewrite the comment that claims `media::scanByExtensions` matches case-sensitively, keeping the membership `.jpg/.jpeg/.png/.webp`; verify `tests/organize/port_tests.cpp:32`'s existing `b.JPG` case still passes with `xmake test-report --tag="[organize]"`.
- [ ] 2.4 Confirm the untouched lists still behave: `src/picture/picture_process.cpp:802-811`'s seven picture types and `src/video/video_info.cpp:31-38`'s six video types keep their membership and reach the shared rule through `scanByExtensions`; verify with `xmake test-report --tag="[picture-process]"` and `--tag="[video-process]"`.

## 3. Verification

- [ ] 3.1 Run `xmake fmt` twice and confirm the second run leaves no diff; run `xmake tidy` and compare its diagnostic count against the baseline taken before the implementation (around 133 warnings), with no new diagnostic in the changed files.
- [ ] 3.2 Run the full `xmake test-report` with zero failures and record the assertion/case counts; run the filtered tags `[media-scanner]`, `[organize]`, `[picture-process]`, `[video-process]` and confirm each is green.

## 4. Planning-artifact review (before implementation)

- [ ] 4.1 Run the planning-artifact stage of the `code-review` skill (one fresh reviewer, Coherence then Ground truth, findings quoting both sides) against proposal, delta specs, design and tasks; record every finding and its verdict (`resolved (<commit>)` / `rejected: <reason>`) here before the implementation commit.

## 5. Code-diff review (before the implementation commit)

- [ ] 5.1 Run the code-diff stage of the `code-review` skill on the implementation diff (Standards, Spec, Leanness as parallel reviewers) and record the findings, verdicts and fix loop here before the implementation commit lands.
