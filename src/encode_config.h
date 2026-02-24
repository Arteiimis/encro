#pragma once

#include <filesystem>
#include <format>
#include <optional>

#include "error_handle.h"

namespace fs = std::filesystem;

struct EncodeConfig {
  std::optional<fs::path> ffmpegPath = "ffmpeg";
  std::optional<fs::path> inputPath;
  std::optional<fs::path> outputPath;
  std::optional<fs::path> outputFormat = "mp4";
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
    if (crf.has_value() && (crf < 0 || crf > 51)) {
      return eh::makeError("CRF value must be between 0 and 51.");
    }
    return {};
  }

  std::string buildCMD() const {
    auto cmd = std::string{ffmpegPath.value().string()};

    if (!inputPath.has_value()) {
      throw std::runtime_error("Input path is required to build ffmpeg command.");
    }
    cmd += std::format(" -i \"{}\"", inputPath->string());

    cmd += std::format(
      " -c:v {} -crf {}",
      videoCodec.value_or("hevc_nvenc"),
      crf.value_or(20)
    );

    const auto outputVidDir = outputPath.value_or(inputPath->parent_path());
    const auto outputVidPath = outputVidDir
                             / std::format("{}.hevc.mp4", inputPath->stem().string());

    cmd += std::format(" \"{}\"", outputVidPath.string());

    if (progressFilePath.has_value()) {
      cmd += std::format(" -progress \"{}\"", progressFilePath->string());
    }

    return cmd;
  }
};
