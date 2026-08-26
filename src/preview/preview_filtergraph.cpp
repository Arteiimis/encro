#include "preview/preview_filtergraph.h"

#include "utils/utils.h"

#include <algorithm>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace preview {

namespace {

#if defined(_WIN32)
constexpr auto kFontFile = "C:/Windows/Fonts/arial.ttf";
#else
constexpr auto kFontFile = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif

int evenRoundDown(int value) {
  return value & ~1;
}

double seconds(std::uint64_t micros) {
  return static_cast<double>(micros) / 1'000'000.0;
}

// Filter option values are tokenized twice (graph parse, then the filter's
// own option parsing); a ':' must be quoted AND backslash-escaped to survive
// both layers. Comma/space need only the quotes.
auto escapeFilterColons(std::string_view text) -> std::string {
  auto result = std::string{};
  result.reserve(text.size());
  for (auto const ch: text) {
    if (ch == ':') {
      result += '\\';
      result += ':';
    } else {
      result += ch;
    }
  }
  return result;
}

auto drawtext(std::string const& text, int fontSize, int y) -> std::string {
  return std::format(
    "drawtext=text='{}':fontfile='{}':fontsize={}:fontcolor=white:x=10:y={}:"
    "box=1:boxcolor=black@0.5:boxborderw=4",
    escapeFilterColons(text),
    escapeFilterColons(kFontFile),
    fontSize,
    y
  );
}

auto segmentLabel(Window const& window) -> std::string {
  auto label = formatTimeRange(window.startUs, window.durationUs);
  if (window.score.has_value()) {
    auto const isVmaf = window.metric == videoquality::QualityMetric::Vmaf;
    auto const isXpsnr = window.metric == videoquality::QualityMetric::Xpsnr;
    auto const metric = videoquality::metricName(window.metric);
    // Comma separator: '|' is a cmd metacharacter that would break batch
    // invocations (unit-test fakes) even inside quoted arguments.
    if (isVmaf) {
      label += std::format(", {} {:.1f}", metric, window.score.value());
    } else if (isXpsnr) {
      label += std::format(", {} {:.2f} dB", metric, window.score.value());
    } else {
      label += std::format(", {} {:.3f}", metric, window.score.value());
    }
  }
  return label;
}

auto joinWithCommas(std::vector<std::string> const& parts) -> std::string {
  return parts | std::views::join_with(',') | std::ranges::to<std::string>();
}

auto videoChainParts(
  FiltergraphSpec const& spec,
  Window const& window,
  std::uint64_t startUs,
  std::uint64_t endUs,
  int inputIndex,
  std::string_view sideLabel,
  std::optional<std::string> const& extraLabel
) -> std::vector<std::string> {
  auto parts = std::vector<std::string>{
    std::format(
      "[{}:v]trim=start={:.6f}:end={:.6f}",
      inputIndex,
      seconds(startUs),
      seconds(endUs)
    ),
    "setpts=PTS-STARTPTS",
  };
  if (spec.original.fps > 0.0) {
    parts.push_back(std::format("fps={:.6f}", spec.original.fps));
  }
  auto const minWidth = evenRoundDown(std::min(spec.original.width, spec.encoded.width));
  auto const minHeight =
    evenRoundDown(std::min(spec.original.height, spec.encoded.height));
  parts.push_back(
    std::format(
      "scale={}:{}:force_original_aspect_ratio=decrease,pad={}:{}:(ow-iw)/2:(oh-ih)/2",
      minWidth,
      minHeight,
      minWidth,
      minHeight
    )
  );
  // Normalize pixel format so hstack/concat see identical formats (a 10-bit
  // original vs an 8-bit encode would otherwise fail).
  parts.push_back("format=yuv420p");
  parts.push_back(drawtext(std::string{sideLabel}, 24, 10));
  if (extraLabel.has_value()) { parts.push_back(drawtext(extraLabel.value(), 18, 38)); }
  return parts;
}

}  // namespace

auto buildPreviewFiltergraph(FiltergraphSpec const& spec) -> std::string {
  auto graph = std::string{};
  auto const windowCount = spec.windows.size();

  for (auto index = std::size_t{}; index < windowCount; ++index) {
    auto const& window = spec.windows[index];
    auto const startUs = window.startUs;
    auto const endUs = window.startUs + window.durationUs;

    auto const originalParts =
      videoChainParts(spec, window, startUs, endUs, 0, "ORIGINAL", segmentLabel(window));
    graph += joinWithCommas(originalParts);
    graph += std::format("[o{}];", index);

    auto const encodedParts = videoChainParts(
      spec,
      window,
      spec.encodedWindowsAreSegments ? 0 : startUs,
      spec.encodedWindowsAreSegments ? window.durationUs : endUs,
      spec.encodedWindowsAreSegments ? static_cast<int>(1 + index) : 1,
      "ENCODED",
      std::nullopt
    );
    graph += joinWithCommas(encodedParts);
    graph += std::format("[e{}];", index);

    graph += std::format("[o{}][e{}]hstack[h{}];", index, index, index);
  }

  for (auto index = std::size_t{}; index < windowCount; ++index) {
    graph += std::format("[h{}]", index);
  }
  graph += std::format("concat=n={}:v=1:a=0[vout]", windowCount);

  if (spec.original.hasAudio && windowCount > 0) {
    graph += ";";
    for (auto index = std::size_t{}; index < windowCount; ++index) {
      auto const& window = spec.windows[index];
      graph += std::format(
        "[0:a]atrim=start={:.6f}:end={:.6f},asetpts=PTS-STARTPTS[a{}];",
        seconds(window.startUs),
        seconds(window.startUs + window.durationUs),
        index
      );
    }
    for (auto index = std::size_t{}; index < windowCount; ++index) {
      graph += std::format("[a{}]", index);
    }
    graph += std::format("concat=n={}:v=0:a=1[aout]", windowCount);
  }

  return graph;
}

auto buildPreviewCommand(
  fs::path const& ffmpegPath,
  fs::path const& originalPath,
  std::vector<fs::path> const& encodedPaths,
  FiltergraphSpec const& spec,
  fs::path const& outputPath
) -> std::string {
  auto cmd = quoteToolPath(ffmpegPath);
  cmd += " -hide_banner -nostats -loglevel error -y";
  cmd += std::format(" -i \"{}\"", originalPath.string());
  for (auto const& encodedPath: encodedPaths) {
    cmd += std::format(" -i \"{}\"", encodedPath.string());
  }
  cmd += std::format(" -filter_complex \"{}\"", buildPreviewFiltergraph(spec));
  cmd += " -map \"[vout]\"";
  if (spec.original.hasAudio && !spec.windows.empty()) {
    cmd += " -map \"[aout]\" -c:a aac -b:a 192k";
  } else {
    cmd += " -an";
  }
  cmd += " -c:v libx264 -crf 14 -preset veryfast -pix_fmt yuv420p";
  cmd += std::format(" \"{}\"", outputPath.string());
  return cmd;
}

}  // namespace preview
