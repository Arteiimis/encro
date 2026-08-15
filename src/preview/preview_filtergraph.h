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
  std::string audioCodec;
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
// trim/fps-normalize/scale-to-min with ORIGINAL/ENCODED/segment labels,
// hstack pairs and concat; windowed audio follows the same segments when the
// original has an audio stream.
auto buildPreviewFiltergraph(FiltergraphSpec const& spec) -> std::string;

// The full preview generation command: x264 crf 14 veryfast, yuv420p, audio
// re-encoded to AAC (filtered audio is decoded, so copy is not possible),
// -y overwrite.
auto buildPreviewCommand(
  std::filesystem::path const& ffmpegPath,
  std::filesystem::path const& originalPath,
  std::filesystem::path const& encodedPath,
  FiltergraphSpec const& spec,
  std::filesystem::path const& outputPath
) -> std::string;

}  // namespace preview
