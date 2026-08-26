#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"
#include "utils/utils.h"
#include "video/video_info.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <thread>

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
  std::optional<int> threads;  // CPU-codec thread cap; unset = ffmpeg auto
  std::optional<fs::path> progressFilePath;
  std::optional<std::uint64_t> segmentIndex;
  std::optional<std::uint64_t> segmentStartUs;
  std::optional<std::uint64_t> segmentDurationUs;
  std::optional<fs::path> tempOutputPath;

  bool operator==(EncodeConfig const&) const = default;

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

    auto format = outputFormat.value_or("mp4");
    auto codec = videoCodec.value_or("hevc_nvenc");

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
    auto cmd = quoteToolPath(ffmpegPath.value_or(fs::path{"ffmpeg"}));
    cmd += " -hide_banner -nostats -loglevel error -y";

    if (!inputPath.has_value()) {
      throw std::runtime_error("Input path is required to build ffmpeg command.");
    }

    auto const isSegmented = segmentIndex.has_value();
    auto const seconds = [](std::uint64_t micros) {
      return std::format("{:.6f}", static_cast<double>(micros) / 1'000'000.0);
    };

    if (isSegmented) {
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): set together with segmentIndex by buildSegmentEncodeConfig
      auto const startSec = seconds(segmentStartUs.value());
      cmd += std::format(" -ss {} -i \"{}\"", startSec, inputPath->string());
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): set together with segmentIndex by buildSegmentEncodeConfig
      cmd += std::format(" -t {}", seconds(segmentDurationUs.value()));
      cmd += " -force_key_frames 0";
      cmd += " -an";
    } else {
      cmd += std::format(" -i \"{}\"", inputPath->string());
    }

    auto format = outputFormat.value_or("mp4");
    auto codec = videoCodec.value_or("hevc_nvenc");
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
      if (threads.has_value()) { cmd += std::format(" -threads {}", threads.value()); }
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

inline int pickMaxrateKbpsForDimensions(int width, int height) {
  auto const pixels = static_cast<std::int64_t>(width) * height;
  if (pixels >= 3'686'400) { return 15000; }  // 2K (2560x1440)
  if (pixels >= 2'073'600) { return 10000; }  // 1080p (1920x1080)
  return 6000;
}

struct EncodeInputSettings {
  std::optional<std::string> nvencPreset;
  std::optional<int> maxrateKbps;
};

// Identity of one segmented encode: which input, which segment, and where
// the temporary output goes. Shared by production encodes and probe encodes
// so both describe the same segment the same way.
struct SegmentEncodeSpec {
  fs::path inputPath;
  std::uint64_t segmentIndex;
  std::uint64_t startUs;
  std::uint64_t durationUs;
  fs::path tempOutputPath;
};

// "How to encode" cluster: format/codec/quality settings plus the worker
// count used to cap CPU-codec threads. Probe encodes reuse the same values
// (only the CQ/crf differs), which keeps probe and production configs
// aligned via the single construction path below.
struct EncodeProfile {
  std::string outputFormat;
  std::optional<std::string> videoCodec;
  std::optional<int> crf;
  EncodeInputSettings settings;
  std::size_t workerCount = 0;  // 0 = ffmpeg auto threads
};

// Resolves the preset/maxrate a production encode of inputPath would use:
// the configured preset when set, otherwise picked by resolution; the
// maxrate cap is always picked by resolution when dimensions are known.
// Probing mirrors this exact resolution so decisions reflect the real encode.
inline auto resolveInputEncodeSettings(
  appctx::ToolchainPaths const& toolchain,
  appctx::RuntimeContext& runtime,
  fs::path const& inputPath,
  std::optional<std::string> const& configuredPreset
) -> EncodeInputSettings {
  auto settings = EncodeInputSettings{};
  if (
    auto const dims = getVidDimensions(toolchain, runtime, inputPath); dims.has_value()
  ) {
    settings.nvencPreset = configuredPreset.has_value()
      ? configuredPreset
      : std::optional<std::string>{
          pickNvencPresetForDimensions(dims->first, dims->second)
        };
    settings.maxrateKbps = pickMaxrateKbpsForDimensions(dims->first, dims->second);
  }
  return settings;
}

// Single construction path for segmented encodes: used by the real encode
// (encodeOneSegment) and by quality probing, so probe encodes differ from
// production only in CQ and output path (invariant asserted by tests).
// workerCount caps CPU-codec threads to hw_threads / workers so N parallel
// encodes do not oversubscribe the machine; 0 leaves threads on ffmpeg auto.
inline auto buildSegmentEncodeConfig(
  appctx::ToolchainPaths const& toolchain,
  SegmentEncodeSpec const& spec,
  EncodeProfile const& profile,
  std::optional<fs::path> progressFilePath = std::nullopt
) -> EncodeConfig {
  auto threads = std::optional<int>{};
  if (profile.workerCount > 0) {
    auto const hw = std::thread::hardware_concurrency();
    if (hw > 0) {
      threads = std::max(1u, hw / static_cast<unsigned>(profile.workerCount));
    }
  }

  return EncodeConfig{
    .ffmpegPath = toolchain.ffmpegPath,
    .inputPath = spec.inputPath,
    .outputFormat = profile.outputFormat,
    .videoCodec = profile.videoCodec,
    .crf = profile.crf,
    .nvencPreset = profile.settings.nvencPreset,
    .maxrateKbps = profile.settings.maxrateKbps,
    .threads = threads,
    .progressFilePath = std::move(progressFilePath),
    .segmentIndex = spec.segmentIndex,
    .segmentStartUs = spec.startUs,
    .segmentDurationUs = spec.durationUs,
    .tempOutputPath = spec.tempOutputPath
  };
}

inline auto buildAudioExtractionCmd(
  fs::path const& ffmpegPath,
  fs::path const& inputPath,
  fs::path const& audioPath,
  bool aacFallback
) -> std::string {
  auto cmd = quoteToolPath(ffmpegPath);
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
  auto cmd = quoteToolPath(ffmpegPath);
  cmd += " -hide_banner -nostats -loglevel error -y";
  cmd += std::format(" -f concat -safe 0 -i \"{}\"", listPath.string());
  if (audioPath.has_value()) { cmd += std::format(" -i \"{}\"", audioPath->string()); }
  cmd += " -map 0:v";
  if (audioPath.has_value()) { cmd += " -map 1:a"; }
  cmd += " -c copy";
  cmd += std::format(" \"{}\"", outputPath.string());
  return cmd;
}
