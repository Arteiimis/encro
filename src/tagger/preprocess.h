// Model input preprocessing as a single ffmpeg invocation per image (design
// D2/D8): decode, composite alpha over white, emit rawvideo rgb24 on stdout —
// the float cast and the layout are the engines' business. The filter graph
// is the part that differs per model contract, so it is the parameter.
#pragma once

#include "core/error_handle.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace tagger {

// Whose input contract the graph must produce.
enum class InputKind {
  Tagger,    // wd-vit-tagger-v3: 448x448, aspect kept, centered on white
  Identity,  // CCIP: 384x384 stretched (the model's own Resize((384, 384)))
};

// wd input contract: NHWC float32 0..255, RGB, no normalization.
inline constexpr auto kInputEdge = 448;
inline constexpr auto kInputBytes =
  std::size_t{kInputEdge} * std::size_t{kInputEdge} * std::size_t{3};

// Identity input contract: NCHW float32 0..1, RGB, plain stretch.
inline constexpr auto kIdentityEdge = 384;
inline constexpr auto kIdentityBytes =
  std::size_t{kIdentityEdge} * std::size_t{kIdentityEdge} * std::size_t{3};

// The pinned preprocessing command for `kind`: white-base overlay
// (correct alpha flattening) and aspect-preserving pad for the tagger, a
// plain stretch for the identity model. Asserted by the [real-ffmpeg]
// contract tests.
auto buildPreprocessCommand(fs::path const& ffmpeg, fs::path const& input, InputKind kind)
  -> std::string;

// Runs the preprocessing and returns exactly `kind`'s byte count of RGB data.
auto runPreprocess(fs::path const& ffmpeg, fs::path const& input, InputKind kind)
  -> eh::Result<std::vector<std::uint8_t>>;

}  // namespace tagger
