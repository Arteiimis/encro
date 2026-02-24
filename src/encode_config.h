#pragma once

#include "error_handle.h"

#include <array>
#include <filesystem>
#include <format>
#include <optional>

namespace fs = std::filesystem;

struct EncodeConfig {
  std::optional<fs::path> ffmpegPath = "ffmpeg";
  std::optional<fs::path> inputPath;
  std::optional<fs::path> outputPath;
  std::optional<std::string> outputFormat = "mp4";
  std::optional<std::string> videoCodec = "hevc_nvenc";
  std::optional<int> crf = 20;
  std::optional<fs::path> progressFilePath;

  eh::Result<void> validate() const {
    if (!inputPath.has_value()) { return eh::makeError("Input path is required."); }
    if (!fs::exists(inputPath.value())) {
      return eh::makeError("Input path does not exist: {}", inputPath->string());
    }
    if (outputFormat.has_value() && outputFormat->empty()) {
      return eh::makeError("Output format cannot be an empty string.");
    }
    constexpr auto validOutputFormats = std::array{"mp4", "webp"};
    if (outputFormat.has_value()
        && !std::ranges::contains(validOutputFormats, outputFormat.value())) {
      return eh::makeError("Output format must be one of: mp4, webp.");
    }
    if (crf.has_value() && (crf < 0 || crf > 51)) {
      return eh::makeError("CRF value must be between 0 and 51.");
    }
    return {};
  }

  fs::path buildOutputPath() const {
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

    auto const outputVidDir = outputPath.value_or(inputPath->parent_path());

    if (format == "webp") {
      return outputVidDir / std::format("{}.{}", inputPath->stem().string(), format);
    }

    return outputVidDir
         / std::format("{}.{}.{}", inputPath->stem().string(), codecTag, format);
  }

  std::string buildCMD() const {
    auto cmd = std::string{ffmpegPath.value().string()};

    if (!inputPath.has_value()) {
      throw std::runtime_error("Input path is required to build ffmpeg command.");
    }
    cmd += std::format(" -i \"{}\"", inputPath->string());

    auto const format = outputFormat.value_or("mp4");
    auto const codec = videoCodec.value_or("hevc_nvenc");
    if (format == "webp") {
      cmd += " -vf \"scale=-2:960:force_original_aspect_ratio=decrease\""
             " -c:v libwebp -q:v 80 -loop 0";
    } else {
      cmd += std::format(" -c:v {} -crf {}", codec, crf.value_or(20));
    }

    auto const outputVidPath = buildOutputPath();

    cmd += std::format(" \"{}\"", outputVidPath.string());

    if (progressFilePath.has_value()) {
      cmd += std::format(" -progress \"{}\"", progressFilePath->string());
    }

    return cmd;
  }
};
