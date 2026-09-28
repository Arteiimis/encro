#include "tagger/preprocess.h"

#include "utils/utils.h"

#include <format>

namespace tagger {

namespace {

// One input contract's graph: the edge it produces and how the frame is laid
// on it. The byte count follows from the edge (rgb24).
struct InputSpec {
  int edge = 0;
  std::string filterGraph;
  std::string backdrop;  // extra ffmpeg input arguments; empty = none
};

auto specOf(InputKind kind) -> InputSpec {
  // The identity model's own transform is a plain stretch with the reference's
  // bilinear resampling filter (ffmpeg's unqualified scale is bicubic, which
  // the reference does not use): no aspect preservation, nothing to center it
  // on.
  if (kind == InputKind::Identity) {
    return InputSpec{
      .edge = kIdentityEdge,
      .filterGraph =
        std::format("scale={0}:{0}:flags=bilinear,format=rgb24", kIdentityEdge),
    };
  }
  // The tagger scales into the canvas keeping aspect, then centers it on a
  // white base (which also flattens alpha).
  return InputSpec{
    .edge = kInputEdge,
    .filterGraph = std::format(
      "[0:v]scale=w={0}:h={0}:force_original_aspect_ratio=decrease:"
      "flags=lanczos[img];"
      "[1:v][img]overlay=x=(W-w)/2:y=(H-h)/2,format=rgb24",
      kInputEdge
    ),
    .backdrop = std::format("-f lavfi -i color=c=white:s={0}x{0} ", kInputEdge),
  };
}

std::size_t rgb24Bytes(int edge) {
  auto const side = static_cast<std::size_t>(edge);
  return side * side * std::size_t{3};
}

}  // namespace

auto buildPreprocessCommand(fs::path const& ffmpeg, fs::path const& input, InputKind kind)
  -> std::string {
  auto const spec = specOf(kind);
  return std::format(
    R"({} -hide_banner -loglevel quiet -y -i "{}" {}-filter_complex "{}" )"
    R"(-frames:v 1 -f rawvideo -)",
    quoteToolPath(ffmpeg),
    input.string(),
    spec.backdrop,
    spec.filterGraph
  );
}

auto runPreprocess(fs::path const& ffmpeg, fs::path const& input, InputKind kind)
  -> eh::Result<std::vector<std::uint8_t>> {
  // -loglevel quiet keeps stderr empty, so the merged capture is the raw
  // frame alone.
  auto const [exitCode, output, _, stderrText] =
    exec2(buildPreprocessCommand(ffmpeg, input, kind));
  if (exitCode != 0) {
    return eh::makeError(
      "ffmpeg preprocess failed (exit {}) for {}",
      exitCode,
      input.string()
    );
  }
  auto const expected = rgb24Bytes(specOf(kind).edge);
  if (output.size() != expected) {
    return eh::makeError(
      "ffmpeg preprocess produced {} bytes, expected {}",
      output.size(),
      expected
    );
  }
  return std::vector<std::uint8_t>{output.begin(), output.end()};
}

}  // namespace tagger
