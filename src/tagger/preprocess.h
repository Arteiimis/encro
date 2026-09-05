// Tagger preprocessing as a single ffmpeg invocation (design D2): decode,
// composite alpha over white, scale/pad to a square 448x448 canvas, and emit
// rawvideo rgb24 on stdout — the wd tagger contract (NHWC float32 0..255,
// RGB, no normalization) minus the float cast.
#pragma once

#include "core/error_handle.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace tagger {

inline constexpr auto kInputEdge = 448;
inline constexpr auto kInputBytes = std::size_t{kInputEdge * kInputEdge * 3};

// The pinned preprocessing command: white-base overlay (correct alpha
// flattening), aspect-preserving scale + pad, raw RGB stdout. Asserted by the
// [real-ffmpeg] contract test.
auto buildPreprocessCommand(fs::path const& ffmpeg, fs::path const& input) -> std::string;

// Runs the preprocessing and returns exactly kInputBytes of RGB data.
auto runPreprocess(fs::path const& ffmpeg, fs::path const& input)
  -> eh::Result<std::vector<std::uint8_t>>;

}  // namespace tagger
