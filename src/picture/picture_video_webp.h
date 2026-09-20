#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace picturewebp {

namespace fs = std::filesystem;

// One video of a picture run: the clip, the cached WebP it converts to, and
// the archive entry name the pack step uses (planned by the caller, which owns
// the picture naming scheme).
struct ConversionTask {
  fs::path sourcePath;
  fs::path outputPath;
  std::string entryName;
};

struct ConversionOutcome {
  bool canceled = false;
  // Videos whose cached output exists and may be packed.
  std::vector<ConversionTask> ready;
  // Clips whose conversion failed; they are absent from `ready`, so the
  // phase's summary needs the count to report a total the classes add up to.
  std::size_t failedCount = 0;
};

// Converts the videos a picture run has queued: drops cached outputs the saved
// state does not back, registers one job-state task per clip, encodes what is
// missing through the shared video WebP recipe, and reports which clips may be
// packed. Packing must wait for this to return.
auto runConversionPhase(
  appctx::AppContext& ctx,
  std::span<ConversionTask const> tasks,
  std::size_t maxParallel
) -> eh::Result<ConversionOutcome>;

}  // namespace picturewebp
