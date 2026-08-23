#include "cmd/config_builder.h"

#include "core/path_roots.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using pathroots::commonAncestorPath;
using pathroots::normalizeInputRootDir;

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
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

auto readOutputLayout(CmdParseResult const& result) -> eh::Result<appctx::OutputLayout> {
  if (result.keep) { return appctx::OutputLayout::Keep; }
  return appctx::OutputLayout::Flat;
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

  auto sharedDir = normalizeInputRootDir(inputPaths.front());
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

namespace {

auto resolveSingleInputPath(CmdParseResult const& result) -> eh::Result<fs::path> {
  auto const& input = [&]() -> std::string const& {
    if (result.input.has_value()) { return *result.input; }
    return result.positionalInputs.value().front();
  }();
  auto path = fs::absolute(fs::path{input}).lexically_normal();
  if (auto const exists = requireExists(path, "input"); !exists) {
    return eh::makeError("{}", exists.error());
  }
  return path;
}

auto resolveInputPaths(std::vector<std::string> const& inputs)
  -> eh::Result<std::vector<fs::path>> {
  if (inputs.empty()) { return eh::makeError("Input path is required."); }

  auto paths = std::vector<fs::path>{};
  paths.reserve(inputs.size());
  for (auto const& input: inputs) {
    auto const path = fs::absolute(fs::path{input}).lexically_normal();
    if (auto const exists = requireExists(path, "input"); !exists) {
      return eh::makeError("{}", exists.error());
    }
    if (auto const regular = requireRegularFile(path, "input"); !regular) {
      return eh::makeError("{}", regular.error());
    }
    paths.emplace_back(path);
  }
  return paths;
}

auto applyMediaOptionValidations(appctx::AppConfig& config, CmdParseResult const& result)
  -> eh::Result<void> {
  if (config.processType != "picture" && config.compressImages) {
    return eh::makeError("--compress is only supported with --type picture.");
  }

  // Value checks for -q/--crf/--min-vmaf moved to parse time (CLI::Range);
  // --image-quality requires --compress moved to Option::needs().
  config.imageQuality = result.imageQuality;
  config.crf = result.crf;

  config.minVmaf = result.minVmaf;
  config.dryRun = result.dryRun;

  config.nvencPreset = result.nvencPreset;
  if (config.nvencPreset.has_value() && config.nvencPreset.value() == "auto") {
    config.nvencPreset.reset();  // "auto" = pick by resolution
  }

  config.videoCodec = result.videoCodec;
  return {};
}

auto applyInputSelection(appctx::AppConfig& config, CmdParseResult const& result)
  -> eh::Result<void> {
  // Positional-vs-(-i/-I) and -i-vs--I conflicts are rejected at parse time by
  // native excludes(); only the remaining checks live here.
  auto const hasPositionalInputs = result.positionalInputs.has_value();
  auto const positionalCount =
    hasPositionalInputs ? result.positionalInputs.value().size() : std::size_t{0};
  auto const effectiveSingleInput = result.input.has_value() || positionalCount == 1;
  auto const effectiveMultiInputs = result.inputs.has_value() || positionalCount > 1;

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

    auto const& inputs = [&]() -> std::vector<std::string> const& {
      if (result.inputs.has_value()) { return *result.inputs; }
      return *result.positionalInputs;
    }();
    auto resolved = resolveInputPaths(inputs);
    if (!resolved) { return eh::makeError("{}", resolved.error()); }
    config.inputPaths = std::move(resolved.value());
  } else {
    if (auto const input = resolveSingleInputPath(result); !input) {
      return eh::makeError("{}", input.error());
    } else {
      config.inputPath = input.value();
    }
  }
  return {};
}

}  // namespace

auto buildConfig(CmdParseResult const& result) -> eh::Result<appctx::AppConfig> {
  auto config = appctx::AppConfig{};

  config.resumeState = result.resume;
  config.restartState = result.restart;

  // Canonical values and validation come from parse time (CheckedTransformer
  // for -t, IsMember for -f/--force-conflict-handling, PositiveNumber for -j).
  config.processType = result.processType;
  config.outputFormat = result.outputFormat;
  config.maxParallelJobs = result.maxJobs;

  auto layoutRes = readOutputLayout(result);
  if (!layoutRes) { return eh::makeError("{}", layoutRes.error()); }
  config.outputLayout = layoutRes.value();

  config.forceNameConflictHandling = result.forceConflictHandling == "y";

  auto pictureFolderSummaryRes = readPictureFolderSummary(result);
  if (!pictureFolderSummaryRes) {
    return eh::makeError("{}", pictureFolderSummaryRes.error());
  }
  config.pictureFolderSummary = pictureFolderSummaryRes.value();

  config.compressImages = result.compress;
  if (auto const applied = applyMediaOptionValidations(config, result); !applied) {
    return eh::makeError("{}", applied.error());
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

  if (auto const applied = applyInputSelection(config, result); !applied) {
    return eh::makeError("{}", applied.error());
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
