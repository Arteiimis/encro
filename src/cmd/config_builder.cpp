#include "cmd/config_builder.h"

#include "core/path_roots.h"
#include "utils/utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using pathroots::commonAncestorPath;
using pathroots::normalizeInputRootDir;

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

auto requireDirIfExists(fs::path const& path, std::string_view label)
  -> eh::Result<void> {
  if (!fs::exists(path)) { return {}; }
  return requireDir(path, label);
}

auto requireExists(fs::path const& path, std::string_view label) -> eh::Result<void> {
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
    return eh::makeError("The specified {} path is not a file: {}", label, path.string());
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
  if (jobs == 0) { return eh::makeError("Invalid jobs value: 0. --jobs must be >= 1."); }

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

auto readForceNameConflictHandling(boost::program_options::variables_map const& vm)
  -> eh::Result<bool> {
  if (!vm.count("force-conflict-handling")) { return true; }

  auto value = getParamStr(vm, "force-conflict-handling");
  std::ranges::transform(value, value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  if (value == "y") { return true; }
  if (value == "n") { return false; }

  return eh::makeError("--force-conflict-handling must be set to y or n.");
}

auto readPictureFolderSummary(boost::program_options::variables_map const& vm)
  -> eh::Result<bool> {
  return vm.count("folder-summary") > 0;
}

enum class OutputPathAliasKind {
  Input,
  Common,
};

struct ParsedOutputPathAlias {
  OutputPathAliasKind kind;
  std::string suffix;
};

auto trimLeadingDirectorySeparators(std::string_view text) -> std::string_view {
  while (!text.empty() && (text.front() == '/' || text.front() == '\\')) {
    text.remove_prefix(1);
  }
  return text;
}

auto parseOutputPathAlias(std::string_view raw) -> std::optional<ParsedOutputPathAlias> {
  auto const parseShort = [&](char aliasChar, OutputPathAliasKind kind) {
    if (raw == std::string_view{&aliasChar, 1}) {
      return std::optional<ParsedOutputPathAlias>{ParsedOutputPathAlias{kind, ""}};
    }

    if (
      raw.size() >= 2 && raw.front() == aliasChar && (raw[1] == '/' || raw[1] == '\\')
    ) {
      return std::optional<ParsedOutputPathAlias>{ParsedOutputPathAlias{
        kind,
        std::string{trimLeadingDirectorySeparators(raw.substr(2))}
      }};
    }

    return std::optional<ParsedOutputPathAlias>{};
  };

  if (auto parsed = parseShort('+', OutputPathAliasKind::Input); parsed.has_value()) {
    return parsed;
  }
  if (auto parsed = parseShort('=', OutputPathAliasKind::Common); parsed.has_value()) {
    return parsed;
  }

  constexpr auto kInputPrefix = std::string_view{"input://"};
  if (raw.starts_with(kInputPrefix)) {
    return ParsedOutputPathAlias{
      OutputPathAliasKind::Input,
      std::string{trimLeadingDirectorySeparators(raw.substr(kInputPrefix.size()))}
    };
  }

  constexpr auto kCommonPrefix = std::string_view{"common://"};
  if (raw.starts_with(kCommonPrefix)) {
    return ParsedOutputPathAlias{
      OutputPathAliasKind::Common,
      std::string{trimLeadingDirectorySeparators(raw.substr(kCommonPrefix.size()))}
    };
  }

  return std::nullopt;
}

auto resolveSharedInputDir(std::span<fs::path const> inputPaths)
  -> std::optional<fs::path> {
  if (inputPaths.empty()) { return std::nullopt; }

  auto const sharedDir = normalizeInputRootDir(inputPaths.front());
  for (auto const& inputPath: inputPaths) {
    if (normalizeInputRootDir(inputPath) != sharedDir) { return std::nullopt; }
  }

  return sharedDir;
}

auto resolveCommonInputDir(std::span<fs::path const> inputPaths)
  -> std::optional<fs::path> {
  if (inputPaths.empty()) { return std::nullopt; }

  auto commonDir = std::optional<fs::path>{normalizeInputRootDir(inputPaths.front())};
  for (auto const& inputPath: inputPaths) {
    commonDir = commonAncestorPath(commonDir.value(), normalizeInputRootDir(inputPath));
    if (!commonDir.has_value()) { return std::nullopt; }
  }

  return commonDir;
}

auto resolveOutputAliasBasePath(appctx::AppConfig const& config, OutputPathAliasKind kind)
  -> eh::Result<fs::path> {
  if (!config.inputPaths.empty()) {
    auto const basePath = kind == OutputPathAliasKind::Input
      ? resolveSharedInputDir(config.inputPaths)
      : resolveCommonInputDir(config.inputPaths);
    if (basePath.has_value()) { return basePath.value(); }

    if (kind == OutputPathAliasKind::Input) {
      return eh::makeError(
        "Output alias +/input:// requires all input files to share the same parent "
        "directory."
      );
    }

    return eh::makeError(
      "Output alias =/common:// requires all input files to share a common ancestor "
      "directory."
    );
  }

  if (config.inputPath.empty()) {
    return eh::makeError("Input path is required before resolving output aliases.");
  }

  return normalizeInputRootDir(config.inputPath);
}

auto resolveOutputPathSpec(appctx::AppConfig const& config, std::string_view raw)
  -> eh::Result<fs::path> {
  auto const alias = parseOutputPathAlias(raw);
  if (!alias.has_value()) { return fs::path{raw}; }

  auto basePathRes = resolveOutputAliasBasePath(config, alias->kind);
  if (!basePathRes) { return eh::makeError("{}", basePathRes.error()); }

  auto resolvedPath = basePathRes.value();
  if (alias->suffix.empty()) { return resolvedPath.lexically_normal(); }

  auto const suffixPath = fs::path{alias->suffix};
  if (suffixPath.is_absolute() || suffixPath.has_root_name()) {
    return eh::makeError(
      "Output alias suffix must be a relative path: {}",
      alias->suffix
    );
  }

  resolvedPath /= suffixPath;
  return resolvedPath.lexically_normal();
}

}  // namespace

namespace cmd {

auto buildConfig(boost::program_options::variables_map const& vm)
  -> eh::Result<appctx::AppConfig> {
  auto config = appctx::AppConfig{};

  config.resumeState = vm.count("resume") > 0;
  config.restartState = vm.count("restart") > 0;

  if (config.resumeState && config.restartState) {
    return eh::makeError("--resume and --restart cannot be used together.");
  }

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

  auto forceNamingRes = readForceNameConflictHandling(vm);
  if (!forceNamingRes) { return eh::makeError("{}", forceNamingRes.error()); }
  config.forceNameConflictHandling = forceNamingRes.value();

  auto pictureFolderSummaryRes = readPictureFolderSummary(vm);
  if (!pictureFolderSummaryRes) {
    return eh::makeError("{}", pictureFolderSummaryRes.error());
  }
  config.pictureFolderSummary = pictureFolderSummaryRes.value();

  config.compressImages = vm.count("compress") > 0;

  if (config.processType != "picture" && config.compressImages) {
    return eh::makeError("--compress is only supported when --type is picture.");
  }

  if (vm.count("image-quality")) {
    auto const quality = vm.at("image-quality").as<int>();
    if (quality < 2 || quality > 31) {
      return eh::makeError("--image-quality must be between 2 and 31.");
    }
    config.imageQuality = quality;

    if (!config.compressImages) {
      return eh::makeError("--image-quality requires --compress to be enabled.");
    }
  }

  config.yesToAll = vm.count("yes") > 0;
  config.recursive = vm.count("recursive") > 0;
  config.packOutput = vm.count("pack") > 0;
  config.packOnly = vm.count("pack-only") > 0;
  config.verbose = vm.count("verbose") > 0;
  config.verboseEcho = vm.count("verbose-echo") > 0;

  if (vm.count("state-file")) {
    config.stateFilePath =
      fs::absolute(fs::path{getParamStr(vm, "state-file")}).lexically_normal();
  }

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
    if (inputs.empty()) { return eh::makeError("Input path is required."); }

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
    config.inputPath =
      fs::absolute(fs::path{getParamStr(vm, "input")}).lexically_normal();
    if (auto const exists = requireExists(config.inputPath, "input"); !exists) {
      return eh::makeError("{}", exists.error());
    }
  }

  if (vm.count("output")) {
    auto outputPathRes = resolveOutputPathSpec(config, getParamStr(vm, "output"));
    if (!outputPathRes) { return eh::makeError("{}", outputPathRes.error()); }

    config.outputPath = outputPathRes.value();
    if (
      auto const validDir = requireDirIfExists(config.outputPath.value(), "output");
      !validDir
    ) {
      return eh::makeError("{}", validDir.error());
    }
  }

  return config;
}

}  // namespace cmd
