#include "cmd/config_builder.h"

#include "utils/utils.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

auto requireDir(fs::path const& path, std::string_view label) -> eh::Result<void> {
  if (!fs::is_directory(path)) {
    return eh::makeError(
      "The specified {} path is not a directory: {}",
      label,
      path.string()
    );
  }

  return {};
}

auto requireExists(fs::path const& path, std::string_view label)
  -> eh::Result<void> {
  if (!fs::exists(path)) {
    return eh::makeError(
      "The specified {} path/file does not exist: {}",
      label,
      path.string()
    );
  }

  return {};
}

auto readProcessType(boost::program_options::variables_map const& vm)
  -> eh::Result<std::string> {
  if (!vm.count("type")) { return std::string{"video"}; }

  auto const typeStr = getParamStr(vm, "type");
  constexpr auto validTypes = std::array{"video", "picture"};
  if (!std::ranges::contains(validTypes, typeStr)) {
    return eh::makeError(
      "Invalid process type: {}. Valid types are: video, picture.",
      typeStr
    );
  }

  return typeStr;
}

auto readOutputFormat(boost::program_options::variables_map const& vm)
  -> eh::Result<std::string> {
  if (!vm.count("output-format")) { return std::string{"mp4"}; }

  auto const outputFormat = getParamStr(vm, "output-format");
  constexpr auto validFormats = std::array{"mp4", "webp"};
  if (!std::ranges::contains(validFormats, outputFormat)) {
    return eh::makeError(
      "Invalid output format: {}. Valid formats are: mp4, webp.",
      outputFormat
    );
  }

  return outputFormat;
}

}  // namespace

namespace cmd {

auto buildConfig(boost::program_options::variables_map const& vm)
  -> eh::Result<appctx::AppConfig> {
  auto config = appctx::AppConfig{};

  auto typeRes = readProcessType(vm);
  if (!typeRes) { return eh::makeError("{}", typeRes.error()); }
  config.processType = typeRes.value();

  auto formatRes = readOutputFormat(vm);
  if (!formatRes) { return eh::makeError("{}", formatRes.error()); }
  config.outputFormat = formatRes.value();

  config.yesToAll = vm.count("yes") > 0;
  config.recursive = vm.count("recursive") > 0;
  config.packOutput = vm.count("pack") > 0;
  config.packOnly = vm.count("pack-only") > 0;

  if (vm.count("ffmpeg-path")) {
    auto const iptPath = fs::path{getParamStr(vm, "ffmpeg-path")};
    if (auto const validDir = requireDir(iptPath, "FFmpeg"); !validDir) {
      return eh::makeError("{}", validDir.error());
    }
    config.ffmpegInstallDir = iptPath;
  }

  if (!vm.count("input")) { return eh::makeError("Input path is required."); }

  config.inputPath = fs::path{getParamStr(vm, "input")};
  if (auto const exists = requireExists(config.inputPath, "input"); !exists) {
    return eh::makeError("{}", exists.error());
  }

  if (vm.count("output")) {
    config.outputPath = fs::path{getParamStr(vm, "output")};
    if (auto const validDir = requireDir(config.outputPath.value(), "output");
        !validDir) {
      return eh::makeError("{}", validDir.error());
    }
  }

  return config;
}

}  // namespace cmd
