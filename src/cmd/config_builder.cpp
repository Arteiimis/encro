#include "cmd/config_builder.h"

#include "utils/utils.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

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

auto requireRegularFile(fs::path const& path, std::string_view label)
  -> eh::Result<void> {
  if (!fs::is_regular_file(path)) {
    return eh::makeError(
      "The specified {} path is not a file: {}",
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
  if (typeStr == "vid") { return std::string{"video"}; }
  if (typeStr == "pic") { return std::string{"picture"}; }

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

auto readMaxParallelJobs(boost::program_options::variables_map const& vm)
  -> eh::Result<std::optional<std::size_t>> {
  if (!vm.count("jobs")) { return std::nullopt; }

  auto const jobs = vm.at("jobs").as<std::size_t>();
  if (jobs == 0) {
    return eh::makeError("Invalid jobs value: 0. --jobs must be >= 1.");
  }

  return jobs;
}

auto readOutputLayout(boost::program_options::variables_map const& vm)
  -> eh::Result<appctx::OutputLayout> {
  auto const useFlat = vm.count("flat") > 0;
  auto const useKeep = vm.count("keep") > 0;

  if (useFlat && useKeep) {
    return eh::makeError("--flat and --keep cannot be used together.");
  }

  if (useKeep) { return appctx::OutputLayout::Keep; }

  return appctx::OutputLayout::Flat;
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

  auto jobsRes = readMaxParallelJobs(vm);
  if (!jobsRes) { return eh::makeError("{}", jobsRes.error()); }
  config.maxParallelJobs = jobsRes.value();

  auto layoutRes = readOutputLayout(vm);
  if (!layoutRes) { return eh::makeError("{}", layoutRes.error()); }
  config.outputLayout = layoutRes.value();

  config.yesToAll = vm.count("yes") > 0;
  config.recursive = vm.count("recursive") > 0;
  config.packOutput = vm.count("pack") > 0;
  config.packOnly = vm.count("pack-only") > 0;
  config.forceNameConflictHandling = vm.count("force-conflict-handling") > 0;
  config.verbose = vm.count("verbose") > 0;
  config.verboseEcho = vm.count("verbose-echo") > 0;

  if (vm.count("ffmpeg-path")) {
    auto const iptPath = fs::path{getParamStr(vm, "ffmpeg-path")};
    if (auto const validDir = requireDir(iptPath, "FFmpeg"); !validDir) {
      return eh::makeError("{}", validDir.error());
    }
    config.ffmpegInstallDir = iptPath;
  }

  auto const hasSingleInput = vm.count("input") > 0;
  auto const hasMultiInputs = vm.count("inputs") > 0;

  if (hasSingleInput && hasMultiInputs) {
    return eh::makeError("Use either -i/--input or -I/--inputs, not both.");
  }

  if (!hasSingleInput && !hasMultiInputs) {
    return eh::makeError("Input path is required.");
  }

  if (hasMultiInputs) {
    if (config.processType != "video") {
      return eh::makeError("-I/--inputs is only supported for video type.");
    }

    if (config.packOnly) {
      return eh::makeError("-I/--inputs is not supported with pack-only.");
    }

    auto const inputs = vm.at("inputs").as<std::vector<std::string>>();
    if (inputs.empty()) {
      return eh::makeError("Input path is required.");
    }

    config.inputPaths.reserve(inputs.size());
    for (auto const& input: inputs) {
      auto const path = fs::absolute(fs::path{input}).lexically_normal();
      if (auto const exists = requireExists(path, "input"); !exists) {
        return eh::makeError("{}", exists.error());
      }
      if (auto const regular = requireRegularFile(path, "input"); !regular) {
        return eh::makeError("{}", regular.error());
      }
      config.inputPaths.emplace_back(path);
    }
  } else {
    config.inputPath = fs::absolute(fs::path{getParamStr(vm, "input")}).lexically_normal();
    if (auto const exists = requireExists(config.inputPath, "input"); !exists) {
      return eh::makeError("{}", exists.error());
    }
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
