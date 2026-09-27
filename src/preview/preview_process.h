#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"
#include "preview/preview_filtergraph.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace preview {

struct PreviewOptions {
  fs::path original;
  std::optional<fs::path> encoded;  // absent: single-input mode (probe + encode windows)
  std::optional<fs::path> output;
  std::optional<double> startSeconds;
  std::optional<double> durationSeconds;
  bool noOpen = false;
};

// 5 uniform 10s windows; full comparison for videos shorter than the window
// budget; manual mode returns the single clamped window (error when --start
// is beyond the shorter input's duration).
auto pickPreviewWindows(
  std::uint64_t shorterDurationUs,
  std::optional<std::pair<double, double>> manualRange = std::nullopt
) -> eh::Result<std::vector<Window>>;

// The window phases run on the media-item stage runner, which draws no bar of
// its own here: preview owns one phase-spanning bar and writes it through the
// stage's postfix, so the per-completion text and the stage's slice of the
// bar's value live here, where the flow's guards can pin them.
inline auto windowProgressText(std::size_t done, std::size_t total) -> std::string {
  return std::format("Encoding windows: {}/{}", done, total);
}

inline auto scoringProgressText(std::size_t done, std::size_t total) -> std::string {
  return std::format("Scoring windows: {}/{}", done, total);
}

// The stage's slice of the single phase bar: it spans `base`→85% of the bar
// and reports `done` of `total` completions.
inline float phaseProgressValue(std::size_t done, std::size_t total, float base) {
  return base + (85.0f - base) * static_cast<float>(done) / static_cast<float>(total);
}

// Validates the input(s), scores the windows (unless manual mode), generates
// the side-by-side comparison video and opens it unless --no-open. With
// options.encoded absent, probes the source and compares against windows
// encoded with the chosen CQ.
auto run(appctx::AppContext& ctx, PreviewOptions const& options) -> eh::Result<int>;

}  // namespace preview
