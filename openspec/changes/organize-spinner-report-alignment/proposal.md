## Why

`encro organize` spends its entire model-loading phase (measured ~2s warm, worse cold: CUDA/cuDNN DLL loads plus two ONNX session creations) in terminal silence — the first output line, the provider notice, only prints after both engines are built. The run-final summary table then breaks alignment whenever a generated folder name exceeds its hard-coded 30-character column: `std::format("{:<30}")` pads but never truncates, so long `unknown_*` trait names push the images/source columns right on their rows.

## What Changes

- `ProgressContext` gains an indeterminate spinner bar mode for work with no progress signal (engine loading today, other phases later), animated by the context's existing repaint clock and cleared by the existing bar-lifecycle rules.
- `encro organize` shows that spinner while the tagger and identity engines load; the one provider notice still prints exactly once, after the spinner clears.
- The organize summary table adopts the encode-probe table layout: folder-column width derived from the actual folder names and the terminal width, display-width-aware padding, and ellipsis truncation of over-wide names instead of column overflow.
- `padToDisplayWidth` moves from `encode_probe.cpp` into `core/display_text.h` so both tables share one padding helper (behavior-preserving move).
- No provider-negotiation or engine-construction performance work: on the target machine (CUDA provider initializes on the first attempt) the ladder has no wasted attempts to remove; the cold-start cost is DLL loading, which a spinner makes visible rather than fast.

## Capabilities

### New Capabilities

- `progress-indeterminate-bar`: indeterminate spinner rendering on the progress-bar system — a bar mode that reports ongoing activity without a progress signal, animates on the context clock, shows no ETA badge, and obeys the existing clear/erase lifecycle.

### Modified Capabilities

- `image-character-organize`: the "Progress and report" requirement gains engine-loading spinner coverage and its summary-table layout rule changes from a fixed 30-character folder column to a dynamic, truncating column.

## Impact

- `src/core/progress.h` / `progress.cpp`: indeterminate bar mode on `ProgressContext` (per-bar flag, repaint-clock animation, ETA suppression).
- `src/organize/organize_command.cpp`: spinner context around `makeEngines`, provider notice after clear.
- `src/organize/report.h` / `report.cpp`: `renderReport` takes the terminal width; dynamic folder column; display-width padding and truncation.
- `src/core/display_text.h` + `src/video/encode_probe.cpp`: `padToDisplayWidth` relocation.
- Tests: progress spinner unit tests, new `renderReport` layout tests (the current case asserts content substrings and stays green; alignment and truncation get new cases), organize command-level spinner wiring where observable.
