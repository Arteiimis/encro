#pragma once

#include "video/video_quality.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace preview {

struct VideoProbe {
  int width = 0;
  int height = 0;
  double fps = 0.0;
  std::uint64_t durationUs = 0;
  bool hasAudio = false;
};

struct Window {
  std::uint64_t startUs;
  std::uint64_t durationUs;
  std::optional<double> score;  // p5 in metric units; nullopt in manual mode
  videoquality::QualityMetric metric = videoquality::QualityMetric::Vmaf;
};

struct FiltergraphSpec {
  VideoProbe original;
  VideoProbe encoded;
  std::vector<Window> windows;
  // Encoded-side inputs for buildPreviewCommand: true = one pre-cut segment
  // file per window; false = the single encoded file, seeked per window.
  // The filtergraph itself is input-layout agnostic: original window i is
  // input i, encoded window i is input windowCount + i, all pre-cut.
  bool encodedWindowsAreSegments = false;
};

// "MM:SS-MM:SS" (or "H:MM:SS-H:MM:SS" for >= 1h inputs) segment label text.
inline auto formatTimeRange(std::uint64_t startUs, std::uint64_t durationUs)
  -> std::string {
  auto const formatTime = [](std::uint64_t totalSeconds) {
    auto const hours = totalSeconds / 3600;
    auto const minutes = (totalSeconds % 3600) / 60;
    auto const seconds = totalSeconds % 60;
    if (hours > 0) { return std::format("{}:{:02d}:{:02d}", hours, minutes, seconds); }
    return std::format("{:02d}:{:02d}", minutes, seconds);
  };
  return std::format(
    "{}-{}",
    formatTime(startUs / 1'000'000),
    formatTime((startUs + durationUs) / 1'000'000)
  );
}

// Single ffmpeg filtergraph building the side-by-side comparison: per-window
// fps-normalize/scale-to-min with ORIGINAL/ENCODED/segment labels, hstack
// pairs and concat. Every input is pre-cut to its window with -ss/-t, so the
// graph has no trims; windowed audio follows the same inputs when the
// original has an audio stream.
auto buildPreviewFiltergraph(FiltergraphSpec const& spec) -> std::string;

// Encoder for the comparison render: mirrors the production video codec
// (default hevc_nvenc) so the render runs on the hardware encoder whenever
// the real encode would; CPU codecs keep fast high-quality settings.
struct PreviewEncoderSettings {
  std::string codec = "hevc_nvenc";
  std::optional<std::string> nvencPreset;
};

// The full preview generation command: production codec (see
// PreviewEncoderSettings) at visually transparent quality, yuv420p, audio
// re-encoded to AAC (filtered audio is decoded, so copy is not possible),
// -y overwrite.
auto buildPreviewCommand(
  std::filesystem::path const& ffmpegPath,
  std::filesystem::path const& originalPath,
  std::vector<std::filesystem::path> const& encodedPaths,
  FiltergraphSpec const& spec,
  std::filesystem::path const& outputPath,
  PreviewEncoderSettings const& encoder = {}
) -> std::string;

}  // namespace preview
