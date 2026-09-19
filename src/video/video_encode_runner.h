#pragma once

#include "core/app_context.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

using function_ref = std::function<void(std::string const&)> const&;

// Encodes one video with the recipe the given output format names (mp4:
// segmented encode, webp: adaptive-quality animated WebP). The format is an
// input, not read from the config: the picture run's conversion keeps its own
// config format while encoding webp.
bool encodeVideo(
  appctx::AppContext& ctx,
  appctx::EncodingState& state,
  std::string_view outputFormat,
  function_ref statusUpdater = {},
  std::size_t workerCount = 1
);

// Writes the concat manifest: one bare `file 'seg_N.ts'` entry per segment, in
// the order the encoder's list recorded them, so entries resolve from the
// manifest's own directory.
bool writeConcatManifest(
  fs::path const& listPath,
  std::span<std::string const> segmentNames
);
