#include "cmd/config_builder.h"

#include "core/path_roots.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

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

DEFINE_LOGGER(logtags::CMD_CONFIG);

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

auto readProcessType(CmdParseResult const& result) -> eh::Result<std::string> {
  auto const typeStr = result.processType;
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

auto readOutputFormat(CmdParseResult const& result) -> eh::Result<std::string> {
  auto const outputFormat = result.outputFormat;
  constexpr auto validFormats = std::array{"mp4", "webp"};
  if (!std::ranges::contains(validFormats, outputFormat)) {
    return eh::makeError(
      "Invalid output format: {}. Valid formats are: mp4, webp.",
      outputFormat
    );
  }

  return outputFormat;
}

auto readMaxParallelJobs(CmdParseResult const& result)
  -> eh::Result<std::optional<std::size_t>> {
  if (!result.maxJobs.has_value()) { return std::nullopt; }

  auto const jobs = result.maxJobs.value();
  if (jobs == 0) { return eh::makeError("Invalid jobs value: 0. --jobs must be >= 1."); }

  return jobs;
}

auto readOutputLayout(CmdParseResult const& result) -> eh::Result<appctx::OutputLayout> {
  if (result.keep) { return appctx::OutputLayout::Keep; }
  return appctx::OutputLayout::Flat;
}

auto readForceNameConflictHandling(CmdParseResult const& result) -> eh::Result<bool> {
  auto value = result.forceConflictHandling;
  std::ranges::transform(value, value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  if (value == "y") { return true; }
  if (value == "n") { return false; }

  return eh::makeError("--force-conflict-handling must be set to y or n.");
}

auto readPictureFolderSummary(CmdParseResult const& result) -> eh::Result<bool> {
  return result.folderSummary;
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

auto buildConfig(CmdParseResult const& result) -> eh::Result<appctx::AppConfig> {
  auto config = appctx::AppConfig{};

  config.resumeState = result.resume;
  config.restartState = result.restart;

  auto typeRes = readProcessType(result);
  if (!typeRes) { return eh::makeError("{}", typeRes.error()); }
  config.processType = typeRes.value();

  auto formatRes = readOutputFormat(result);
  if (!formatRes) { return eh::makeError("{}", formatRes.error()); }
  config.outputFormat = formatRes.value();

  auto jobsRes = readMaxParallelJobs(result);
  if (!jobsRes) { return eh::makeError("{}", jobsRes.error()); }
  config.maxParallelJobs = jobsRes.value();

  auto layoutRes = readOutputLayout(result);
  if (!layoutRes) { return eh::makeError("{}", layoutRes.error()); }
  config.outputLayout = layoutRes.value();

  auto forceNamingRes = readForceNameConflictHandling(result);
  if (!forceNamingRes) { return eh::makeError("{}", forceNamingRes.error()); }
  config.forceNameConflictHandling = forceNamingRes.value();

  auto pictureFolderSummaryRes = readPictureFolderSummary(result);
  if (!pictureFolderSummaryRes) {
    return eh::makeError("{}", pictureFolderSummaryRes.error());
  }
  config.pictureFolderSummary = pictureFolderSummaryRes.value();

  config.compressImages = result.compress;

  if (config.processType != "picture" && config.compressImages) {
    return eh::makeError("--compress is only supported with --type picture.");
  }

  if (result.imageQuality.has_value()) {
    auto const quality = result.imageQuality.value();
    if (quality < 2 || quality > 31) {
      return eh::makeError("--image-quality must be between 2 and 31.");
    }
    config.imageQuality = quality;

    if (!config.compressImages) {
      return eh::makeError("--image-quality requires --compress to be enabled.");
    }
  }

  if (result.crf.has_value()) {
    auto const crf = result.crf.value();
    if (crf < 0 || crf > 51) { return eh::makeError("--crf must be between 0 and 51."); }
    config.crf = crf;
  }

  config.nvencPreset = result.nvencPreset;
  if (config.nvencPreset.has_value() && config.nvencPreset.value() == "auto") {
    config.nvencPreset.reset();  // "auto" = pick by resolution
  }

  config.yesToAll = result.yesToAll;
  config.recursive = result.recursive;
  config.packOutput = result.pack;
  config.packOnly = result.packOnly;
  config.verbose = result.verbose;
  config.fullProgress = result.fullProgress;
  config.jsonEnabled = result.jsonEnabled;

  if (result.stateFile.has_value()) {
    config.stateFilePath =
      fs::absolute(fs::path{result.stateFile.value()}).lexically_normal();
  }

  if (result.ffmpegPath.has_value()) {
    auto const iptPath = fs::path{result.ffmpegPath.value()};
    if (auto const validDir = requireDir(iptPath, "FFmpeg"); !validDir) {
      return eh::makeError("{}", validDir.error());
    }
    config.ffmpegInstallDir = iptPath;
  }

  auto const hasSingleInput = result.input.has_value();
  auto const hasMultiInputs = result.inputs.has_value();
  auto const hasPositionalInputs = result.positionalInputs.has_value();

  if (hasPositionalInputs && (hasSingleInput || hasMultiInputs)) {
    return eh::makeError(
      "Use either positional input paths or -i/--input/-I/--inputs, not both."
    );
  }

  auto const positionalCount =
    hasPositionalInputs ? result.positionalInputs.value().size() : std::size_t{0};
  auto const effectiveSingleInput = hasSingleInput || positionalCount == 1;
  auto const effectiveMultiInputs = hasMultiInputs || positionalCount > 1;

  if (effectiveSingleInput && effectiveMultiInputs) {
    return eh::makeError("Use either -i/--input or -I/--inputs, not both.");
  }

  if (!effectiveSingleInput && !effectiveMultiInputs) {
    return eh::makeError(
      "Input path is required. Pass a directory or file list directly, or use "
      "-i/--input."
    );
  }

  if (effectiveMultiInputs) {
    if (config.processType != "video") {
      return eh::makeError("Multiple input paths are only supported with --type video.");
    }

    if (config.packOnly) {
      return eh::makeError(
        "Multiple input paths cannot be used with --pack-only; pass a single "
        "directory instead."
      );
    }

    auto const& inputs =
      hasMultiInputs ? result.inputs.value() : result.positionalInputs.value();
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
    auto const& input =
      hasSingleInput ? result.input.value() : result.positionalInputs.value().front();
    config.inputPath = fs::absolute(fs::path{input}).lexically_normal();
    if (auto const exists = requireExists(config.inputPath, "input"); !exists) {
      return eh::makeError("{}", exists.error());
    }
  }

  if (result.output.has_value()) {
    auto outputPathRes = resolveOutputPathSpec(config, result.output.value());
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
