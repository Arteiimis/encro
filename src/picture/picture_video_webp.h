#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"
#include "picture/picture_types.h"

#include <cstddef>
#include <span>

namespace picturewebp {

// The phase's result: what the caller cannot read off the items. Which clips
// may be packed follows from their outcomes, so it is not returned again.
struct ConversionOutcome {
  bool canceled = false;
  // Clips whose conversion failed; they are not packable, so the phase's
  // summary needs the count to report a total the classes add up to.
  std::size_t failedCount = 0;
};

// Converts the videos a picture run has queued: drops cached outputs the saved
// state does not back, registers one job-state task per clip, encodes what is
// missing through the shared video WebP recipe, and marks every clip whose
// cached output may be packed (Succeeded for a conversion, Skipped for a cache
// hit). Packing must wait for this to return.
auto runConversionPhase(
  appctx::AppContext& ctx,
  std::span<MediaItem> items,
  std::size_t maxParallel
) -> eh::Result<ConversionOutcome>;

}  // namespace picturewebp
