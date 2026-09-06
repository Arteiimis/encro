#include "tagger/preprocess.h"

#include "utils/utils.h"

#include <format>

namespace tagger {

auto buildPreprocessCommand(fs::path const& ffmpeg, fs::path const& input)
  -> std::string {
  auto const filter = std::format(
    "[0:v]scale=w={0}:h={0}:force_original_aspect_ratio=decrease:"
    "flags=lanczos[img];"
    "[1:v][img]overlay=x=(W-w)/2:y=(H-h)/2,format=rgb24",
    kInputEdge
  );
  return std::format(
    R"({} -hide_banner -loglevel quiet -y -i "{}" )"
    R"(-f lavfi -i color=c=white:s={}x{} )"
    R"(-filter_complex "{}" -frames:v 1 -f rawvideo -)",
    quoteToolPath(ffmpeg),
    input.string(),
    kInputEdge,
    kInputEdge,
    filter
  );
}

auto runPreprocess(fs::path const& ffmpeg, fs::path const& input)
  -> eh::Result<std::vector<std::uint8_t>> {
  // -loglevel quiet keeps stderr empty, so the merged capture is the raw
  // frame alone.
  auto const [exitCode, output, _] = exec2(buildPreprocessCommand(ffmpeg, input));
  if (exitCode != 0) {
    return eh::makeError(
      "ffmpeg preprocess failed (exit {}) for {}",
      exitCode,
      input.string()
    );
  }
  if (output.size() != kInputBytes) {
    return eh::makeError(
      "ffmpeg preprocess produced {} bytes, expected {}",
      output.size(),
      kInputBytes
    );
  }
  return std::vector<std::uint8_t>{output.begin(), output.end()};
}

}  // namespace tagger
