#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"
#include "preview/preview_filtergraph.h"

#include <cstdint>
#include <filesystem>
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

// Validates the input(s), scores the windows (unless manual mode), generates
// the side-by-side comparison video and opens it unless --no-open. With
// options.encoded absent, probes the source and compares against windows
// encoded with the chosen CQ.
auto run(appctx::AppContext& ctx, PreviewOptions const& options) -> eh::Result<int>;

}  // namespace preview
