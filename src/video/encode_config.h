#pragma once

#include "core/error_handle.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <optional>
#include <string>

namespace fs = std::filesystem;

struct EncodeConfig {
  std::optional<fs::path> ffmpegPath = "ffmpeg";
  std::optional<fs::path> inputPath;
  std::optional<fs::path> outputPath;
  std::optional<fs::path> outputFilePath;
  std::optional<std::string> outputFormat;
  std::optional<std::string> videoCodec;
  std::optional<int> crf;
  std::optional<std::string> nvencPreset;
  std::optional<int> maxrateKbps;
  std::optional<int> webpQuality;
  std::optional<fs::path> progressFilePath;
  std::optional<std::uint64_t> segmentIndex;
  std::optional<std::uint64_t> segmentStartUs;
  std::optional<std::uint64_t> segmentDurationUs;
  std::optional<fs::path> tempOutputPath;

  eh::Result<void> validate() const {
    if (!inputPath.has_value()) { return eh::makeError("Input path is required."); }
    if (!fs::exists(inputPath.value())) {
      return eh::makeError("Input path does not exist: {}", inputPath->string());
    }
    if (outputFormat.has_value() && outputFormat->empty()) {
      return eh::makeError("Output format cannot be an empty string.");
    }
    constexpr auto validOutputFormats = std::array{"mp4", "webp"};
    if (
      outputFormat.has_value()
      && !std::ranges::contains(validOutputFormats, outputFormat.value())
    ) {
      return eh::makeError("Output format must be one of: mp4, webp.");
    }
    if (crf.has_value() && (crf < 0 || crf > 51)) {
      return eh::makeError("CRF value must be between 0 and 51.");
    }
    if (webpQuality.has_value() && (webpQuality < 0 || webpQuality > 100)) {
      return eh::makeError("WebP quality must be between 0 and 100.");
    }
    return {};
  }

  std::string buildOutputFileName() const {
    if (!inputPath.has_value()) {
      throw std::runtime_error("Input path is required to build output path.");
    }

    auto const format = outputFormat.value_or("mp4");
    auto const codec = videoCodec.value_or("hevc_nvenc");

    auto const codecTag = [&codec] {
      auto const splitPos = codec.find('_');
      if (splitPos == 0) { return codec; }
      if (splitPos == std::string::npos) { return codec; }
      return codec.substr(0, splitPos);
    }();

    if (format == "webp") {
      return std::format("{}.{}", inputPath->stem().string(), format);
    }

    return std::format("{}.{}.{}", inputPath->stem().string(), codecTag, format);
  }

  fs::path buildOutputPath() const {
    if (!inputPath.has_value()) {
      throw std::runtime_error("Input path is required to build output path.");
    }

    if (outputFilePath.has_value()) { return outputFilePath.value(); }

    auto const outputVidDir = outputPath.value_or(inputPath->parent_path());

    return outputVidDir / buildOutputFileName();
  }

  std::string buildCMD() const {
    auto cmd = std::format("\"{}\"", ffmpegPath.value().string());
    cmd += " -hide_banner -nostats -loglevel error -y";

    if (!inputPath.has_value()) {
      throw std::runtime_error("Input path is required to build ffmpeg command.");
    }

    auto const isSegmented = segmentIndex.has_value();
    auto const seconds = [](std::uint64_t micros) {
      return std::format("{:.6f}", static_cast<double>(micros) / 1'000'000.0);
    };

    if (isSegmented) {
      auto const startSec = seconds(segmentStartUs.value());
      cmd += std::format(" -ss {} -i \"{}\"", startSec, inputPath->string());
      cmd += std::format(" -t {}", seconds(segmentDurationUs.value()));
      cmd += " -force_key_frames 0";
      cmd += " -an";
    } else {
      cmd += std::format(" -i \"{}\"", inputPath->string());
    }

    auto const format = outputFormat.value_or("mp4");
    auto const codec = videoCodec.value_or("hevc_nvenc");
    if (format == "webp") {
      auto const quality = webpQuality.value_or(80);
      cmd += " -vf \"scale=-2:960:force_original_aspect_ratio=decrease\""
        + std::format(" -c:v libwebp -q:v {} -loop 0", quality);
    } else if (codec.ends_with("_nvenc")) {
      cmd += std::format(
        " -c:v {} -preset {} -rc vbr -cq {} -b:v 0",
        codec,
        nvencPreset.value_or("p5"),
        crf.value_or(28)
      );
      if (maxrateKbps.has_value()) {
        cmd += std::format(
          " -maxrate {}k -bufsize {}k",
          maxrateKbps.value(),
          maxrateKbps.value() * 2
        );
      }
      if (codec == "hevc_nvenc") { cmd += " -tag:v hvc1"; }
      cmd += " -pix_fmt yuv420p";
    } else {
      cmd += std::format(" -c:v {} -crf {}", codec, crf.value_or(20));
    }

    if (isSegmented) { cmd += " -f mpegts"; }

    auto const outputVidPath = tempOutputPath.value_or(buildOutputPath());

    cmd += std::format(" \"{}\"", outputVidPath.string());

    if (progressFilePath.has_value()) {
      cmd += std::format(" -progress \"{}\"", progressFilePath->string());
    }

    return cmd;
  }
};

inline auto pickNvencPresetForDimensions(int width, int height) -> std::string {
  auto const pixels = static_cast<std::int64_t>(width) * height;
  if (pixels >= 3'686'400) { return "p7"; }  // 2K (2560x1440)
  if (pixels >= 2'073'600) { return "p6"; }  // 1080p (1920x1080)
  return "p5";
}

inline auto pickMaxrateKbpsForDimensions(int width, int height) -> int {
  auto const pixels = static_cast<std::int64_t>(width) * height;
  if (pixels >= 3'686'400) { return 15000; }  // 2K (2560x1440)
  if (pixels >= 2'073'600) { return 10000; }  // 1080p (1920x1080)
  return 6000;
}

inline auto buildAudioExtractionCmd(
  fs::path const& ffmpegPath,
  fs::path const& inputPath,
  fs::path const& audioPath,
  bool aacFallback
) -> std::string {
  auto cmd = std::format("\"{}\"", ffmpegPath.string());
  cmd += " -hide_banner -nostats -loglevel error -y";
  cmd += std::format(" -i \"{}\"", inputPath.string());
  cmd += aacFallback ? " -vn -c:a aac -b:a 192k" : " -vn -c:a copy";
  cmd += std::format(" \"{}\"", audioPath.string());
  return cmd;
}

inline auto buildSegmentAssemblyCmd(
  fs::path const& ffmpegPath,
  fs::path const& listPath,
  std::optional<fs::path> const& audioPath,
  fs::path const& outputPath
) -> std::string {
  auto cmd = std::format("\"{}\"", ffmpegPath.string());
  cmd += " -hide_banner -nostats -loglevel error -y";
  cmd += std::format(" -f concat -safe 0 -i \"{}\"", listPath.string());
  if (audioPath.has_value()) { cmd += std::format(" -i \"{}\"", audioPath->string()); }
  cmd += " -map 0:v";
  if (audioPath.has_value()) { cmd += " -map 1:a"; }
  cmd += " -c copy";
  cmd += std::format(" \"{}\"", outputPath.string());
  return cmd;
}
