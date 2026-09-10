#pragma once

#include "core/app_context.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>

namespace fs = std::filesystem;

using function_ref = std::function<void(std::string const&)> const&;

bool encodeVideo(
  appctx::AppContext& ctx,
  appctx::EncodingState& state,
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
