#include "cmd/cmd.h"

#include "cmd/option_specs.h"

#include "infra/env.h"
#include "infra/terminal.h"

#include <CLI/CLI.hpp>

#include <array>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace std::literals;
using enum terminal::MessageKind;

namespace {

struct HelpTextLayout {
  unsigned lineLength;
  unsigned minDescriptionLength;
  bool explicitWidthConstraint;
};

auto readHelpColumnsOverride() -> std::optional<unsigned> {
  auto const columns = processenv::readNonEmptyEnvVar("COLUMNS");
  if (!columns.has_value()) { return std::nullopt; }

  auto value = unsigned{0};
  auto const view = std::string_view{columns.value()};
  auto const [end, error] =
    std::from_chars(view.data(), view.data() + view.size(), value);
  if (error != std::errc{} || end != view.data() + view.size() || value == 0) {
    return std::nullopt;
  }

  return value;
}

auto resolveHelpTextLayout() -> HelpTextLayout {
  auto lineLength = 120u;
  auto explicitWidthConstraint = false;
  if (auto const override = readHelpColumnsOverride(); override.has_value()) {
    lineLength = std::clamp(override.value(), 40u, 120u);
    explicitWidthConstraint = true;
  }

  return {
    .lineLength = lineLength,
    .minDescriptionLength = lineLength / 2,
    .explicitWidthConstraint = explicitWidthConstraint,
  };
}

// ── formatter_fn helpers (no color — plain text, Phase 20 adds color) ──

auto formatOptionName(CLI::Option const* opt) -> std::string {
  if (opt->get_positional()) { return opt->get_name(true); }

  auto names = std::string{};
  auto const& lnames = opt->get_lnames();
  auto const& snames = opt->get_snames();
  if (!lnames.empty()) {
    auto first = true;
    for (auto const& ln: lnames) {
      if (!first) names += ',';
      first = false;
      names += "--" + ln;
    }
    if (!snames.empty()) {
      names += ',';
      for (auto const& sn: snames) {
        names += '-' + sn;
        break;  // CLI11 stores short names without dash
      }
    }
  } else if (!snames.empty()) {
    for (auto const& sn: snames) {
      names += '-' + sn;
      break;
    }
  }
  return names;
}

std::size_t countLeadingWhitespace(std::string_view text) {
  auto count = std::size_t{0};
  while (
    count < text.size() && std::isspace(static_cast<unsigned char>(text[count])) != 0
  ) {
    ++count;
  }
  return count;
}

auto wrapDescriptionLine(
  std::string_view line,
  unsigned firstWidth,
  unsigned continuationWidth
) -> std::vector<std::string> {
  auto wrapped = std::vector<std::string>{};
  if (firstWidth == 0 || continuationWidth == 0) {
    wrapped.emplace_back(line);
    return wrapped;
  }

  auto const leadingWhitespace = countLeadingWhitespace(line);
  auto const prefix = std::string(leadingWhitespace, ' ');
  auto remaining = line.substr(leadingWhitespace);
  auto const firstContentWidth =
    std::max<unsigned>(1, firstWidth - static_cast<unsigned>(prefix.size()));
  auto const continuationContentWidth =
    std::max<unsigned>(1, continuationWidth - static_cast<unsigned>(prefix.size()));

  if (remaining.empty()) {
    wrapped.push_back(prefix);
    return wrapped;
  }

  auto isFirstLine = true;
  while (!remaining.empty()) {
    auto const contentWidth = isFirstLine ? firstContentWidth : continuationContentWidth;
    if (remaining.size() <= contentWidth) {
      wrapped.push_back(prefix + std::string{remaining});
      break;
    }

    auto split = remaining.rfind(' ', contentWidth);
    if (split == std::string_view::npos || split == 0) { split = contentWidth; }

    wrapped.push_back(prefix + std::string{remaining.substr(0, split)});
    remaining.remove_prefix(split);
    while (!remaining.empty() && remaining.front() == ' ') { remaining.remove_prefix(1); }
    isFirstLine = false;
  }

  return wrapped;
}

auto wrapDescription(
  std::string_view description,
  unsigned firstWidth,
  unsigned continuationWidth
) -> std::vector<std::string> {
  auto wrapped = std::vector<std::string>{};
  auto start = std::size_t{0};
  auto isFirstLine = true;

  while (start <= description.size()) {
    auto const end = description.find('\n', start);
    auto const line = end == std::string_view::npos
      ? description.substr(start)
      : description.substr(start, end - start);
    auto lines = wrapDescriptionLine(
      line,
      isFirstLine ? firstWidth : continuationWidth,
      continuationWidth
    );
    wrapped.insert(wrapped.end(), lines.begin(), lines.end());

    if (end == std::string_view::npos) { break; }
    start = end + 1;
    isFirstLine = false;
  }

  if (wrapped.empty()) { wrapped.emplace_back(); }

  return wrapped;
}

auto formatOptionHelp(
  CLI::Option const* opt,
  unsigned colWidth,
  unsigned lineLength,
  bool explicitWidthConstraint
) -> std::string {
  auto const nameStr = formatOptionName(opt);

  // Column 1 = name + (=default); a type column is intentionally never
  // rendered, so binding/validator type names cannot leak into help.
  auto defaultText = std::string{};
  auto styledDefaultText = std::string{};
  if (!opt->get_default_str().empty()) {
    defaultText = " (=" + opt->get_default_str() + ")";
    styledDefaultText = terminal::styledText(
      terminal::Stream::Stdout,
      terminal::MessageKind::OptionDefault,
      defaultText
    );
  }

  auto const coloredName = terminal::styledText(
    terminal::Stream::Stdout,
    terminal::MessageKind::OptionName,
    nameStr
  );

  // Pad the full first column (name + default) to colWidth for alignment
  auto const firstCol = nameStr + defaultText;
  auto const gap = firstCol.size() < colWidth ? colWidth - firstCol.size() : 2u;
  auto const displayDescriptionColumn = static_cast<unsigned>(2 + firstCol.size() + gap);
  auto const renderedDescriptionColumn =
    static_cast<unsigned>(2 + coloredName.size() + styledDefaultText.size() + gap);
  auto const indent = std::string(displayDescriptionColumn, ' ');
  auto const firstLineDescriptionColumn =
    explicitWidthConstraint ? renderedDescriptionColumn : displayDescriptionColumn;
  auto const firstLineWidth = firstLineDescriptionColumn < lineLength
    ? lineLength - firstLineDescriptionColumn
    : 1u;
  auto const continuationWidth =
    displayDescriptionColumn < lineLength ? lineLength - displayDescriptionColumn : 1u;

  auto const& description = opt->get_description();
  auto const wrappedDescription =
    wrapDescription(description, firstLineWidth, continuationWidth);
  auto result = std::string{};
  for (auto lineNum = 0u; lineNum < wrappedDescription.size(); ++lineNum) {
    auto const& line = wrappedDescription[lineNum];
    auto const coloredDesc = terminal::styledText(
      terminal::Stream::Stdout,
      terminal::MessageKind::OptionDesc,
      line
    );
    if (lineNum == 0) {
      result += std::format(
        "  {}{}{:<{}}{}\n",
        coloredName,
        styledDefaultText,
        "",
        gap,
        coloredDesc
      );
    } else {
      result += std::format("{}{}\n", indent, coloredDesc);
    }
  }

  if (result.ends_with('\n')) result.pop_back();
  return result;
}

auto formatGroupHeader(std::string const& name) -> std::string {
  if (name.empty()) return {};
  auto const coloredName = terminal::styledText(
    terminal::Stream::Stdout,
    terminal::MessageKind::OptionGroup,
    name
  );
  return std::format("\n{}:\n", coloredName);
}

auto formatIndentedLines(std::span<std::string_view const> lines, unsigned lineLength)
  -> std::string {
  auto result = std::string{};
  constexpr auto indentWidth = 2u;
  auto const contentWidth = lineLength > indentWidth ? lineLength - indentWidth : 1u;

  for (auto const line: lines) {
    auto const wrappedLines = wrapDescriptionLine(line, contentWidth, contentWidth);
    for (auto const& wrappedLine: wrappedLines) {
      result += std::format("  {}\n", wrappedLine);
    }
  }

  if (result.ends_with('\n')) { result.pop_back(); }
  return result;
}

auto formatHelpSection(
  std::string_view title,
  std::span<std::string_view const> lines,
  unsigned lineLength
) -> std::string {
  auto const coloredTitle = terminal::styledText(
    terminal::Stream::Stdout,
    terminal::MessageKind::OptionGroup,
    title
  );

  auto result = std::format("{}:\n", coloredTitle);
  result += formatIndentedLines(lines, lineLength);
  return result;
}

bool hasOptionNames(CLI::Option const* opt) {
  return opt->nonpositional() || opt->get_positional();
}

bool isAdvancedOption(
  CLI::Option const* opt,
  std::span<std::string_view const> advancedLongNames
) {
  auto const& lnames = opt->get_lnames();
  return !lnames.empty()
    && std::ranges::find(advancedLongNames, lnames.front()) != advancedLongNames.end();
}

auto visibleOptionsOf(
  CLI::App const* group,
  CLI::App const* general,
  CLI::App const* appPtr
) -> std::vector<CLI::Option const*> {
  auto opts = std::vector<CLI::Option const*>{};
  if (group == general) {
    for (auto const* opt: appPtr->get_options()) { opts.push_back(opt); }
  }
  for (auto const* opt: group->get_options()) { opts.push_back(opt); }
  return opts;
}

// Direct-binding option registration keeps per-group registration order from
// the old CmdFlagDef arrays (help order contract). long names of advanced
// options only — the -hh/-h tiering list; code list is authoritative (the
// cli-help-tiering main spec's enumeration misses --video-codec).
constexpr auto kAdvancedLongNames = std::array{
  "verbose"sv,
  "log-json"sv,
  "full-progress"sv,
  "color"sv,
  "inputs"sv,
  "state-file"sv,
  "force-conflict-handling"sv,
  "ffmpeg-path"sv,
  "preset"sv,
  "video-codec"sv,
};

auto formatDefaultStr(CLI::Option const* opt) -> std::string {
  auto const defaultStr = opt->get_default_str();
  return defaultStr.empty() ? std::string{} : " (=" + defaultStr + ")";
}

// Max column width across visible options (name + default).
unsigned computeMaxColumnLen(
  CLI::App const* general,
  std::span<CLI::App const* const> groups,
  CLI::App const* appPtr,
  std::span<std::string_view const> advancedLongNames,
  bool fullTier
) {
  auto maxLen = 0u;
  for (auto const* group: groups) {
    for (auto const* opt: visibleOptionsOf(group, general, appPtr)) {
      if (!hasOptionNames(opt)) continue;
      if (!fullTier && isAdvancedOption(opt, advancedLongNames)) continue;
      auto const nameStr = formatOptionName(opt);
      maxLen = std::max(
        maxLen,
        static_cast<unsigned>(nameStr.size() + formatDefaultStr(opt).size())
      );
    }
  }
  return maxLen;
}

auto makeHelpFormatter(
  CLI::App const* general,
  CLI::App const* io,
  CLI::App const* processing,
  CLI::App const* fileop,
  CLI::Option const* helpOpt,
  std::span<std::string_view const> advancedLongNames
) -> auto {
  return  //
    [general, io, processing, fileop, helpOpt, advancedLongNames](
      CLI::App const* app_ptr,
      std::string /*prev*/
      ,  // NOLINT(performance-unnecessary-value-param): CLI11 formatter callback signature is fixed
      CLI::AppFormatMode /*mode*/
    ) -> std::string {
      constexpr auto usageLines = std::array{
        "encro [<input>... | -i <input> | -I <file>...] [-o <output>] [-f mp4|webp] [-r] [-j <n>] [-p] [--resume|--restart]"sv,
        "encro -t picture <input> [-c [-q <n>]] [-s] [-p]"sv,
        "encro -z <input> [-o <output>]"sv,
        "encro preview <original> [<encoded>] [--start <s>] [--duration <s>] [--output <path>] [--no-open]"sv,
        "encro -h | -hh | --version"sv,
      };
      auto const fullTier = helpOpt->count() >= 2;
      constexpr auto hintLine = "Run 'encro -hh' to view all options."sv;

      auto result = std::string{};
      auto const layout = resolveHelpTextLayout();
      auto const desc = app_ptr->get_description();
      if (!desc.empty()) {
        result += terminal::styledText(
          terminal::Stream::Stdout,
          terminal::MessageKind::Usage,
          desc
        );
        result += "\n\n";
      }
      result += formatHelpSection("Usage", std::span{usageLines}, layout.lineLength);
      result += "\n";
      auto const groupIter = std::array{general, io, processing, fileop};
      auto const maxColumnLen = computeMaxColumnLen(
        general,
        std::span{groupIter},
        app_ptr,
        advancedLongNames,
        fullTier
      );

      auto const maxColWidthFromLayout =
        layout.lineLength > layout.minDescriptionLength + 2
        ? layout.lineLength - layout.minDescriptionLength - 2
        : 1u;
      auto const colWidth = std::clamp(
        maxColumnLen,
        std::min(34u, maxColWidthFromLayout),
        std::min(48u, maxColWidthFromLayout)
      );

      for (auto const* group: groupIter) {
        result += formatGroupHeader(group->get_description());
        for (auto const* opt: visibleOptionsOf(group, general, app_ptr)) {
          if (!hasOptionNames(opt)) continue;
          if (!fullTier && isAdvancedOption(opt, advancedLongNames)) continue;
          result += formatOptionHelp(
            opt,
            colWidth,
            layout.lineLength,
            layout.explicitWidthConstraint
          );
          result += '\n';
        }
      }

      if (!fullTier) {
        result += "\n";
        result += terminal::styledText(
          terminal::Stream::Stdout,
          terminal::MessageKind::Hint,
          hintLine
        );
      }

      return result;
    };
}

// Preview subcommand help: rendered from the subcommand's own option
// definitions with the same style helpers as the main help. It deliberately
// does not reuse makeHelpFormatter, which renders the whole main option
// table (captured parent group pointers).
auto makePreviewHelpFormatter(CLI::App const* previewApp) -> auto {
  return  //
    [previewApp](
      CLI::App const* appPtr,
      std::string /*prev*/
      ,  // NOLINT(performance-unnecessary-value-param): CLI11 formatter callback signature is fixed
      CLI::AppFormatMode /*mode*/
    ) -> std::string {
      constexpr auto usageLines = std::array{
        "encro preview <original> [<encoded>] [--start <s>] [--duration <s>] [--output <path>] [--no-open]"sv,
      };
      auto result = std::string{};
      auto const layout = resolveHelpTextLayout();
      auto const desc = appPtr->get_description();
      if (!desc.empty()) {
        result += terminal::styledText(
          terminal::Stream::Stdout,
          terminal::MessageKind::Usage,
          desc
        );
        result += "\n\n";
      }
      result += formatHelpSection("Usage", std::span{usageLines}, layout.lineLength);
      result += "\n";

      // Same column-width logic as the main formatter, over the subcommand's
      // own options only (general=nullptr keeps visibleOptionsOf from
      // double-adding the app-level options).
      auto const maxColumnLen =
        computeMaxColumnLen(nullptr, std::span{&previewApp, 1}, previewApp, {}, true);
      auto const maxColWidthFromLayout =
        layout.lineLength > layout.minDescriptionLength + 2
        ? layout.lineLength - layout.minDescriptionLength - 2
        : 1u;
      auto const colWidth = std::clamp(
        maxColumnLen,
        std::min(34u, maxColWidthFromLayout),
        std::min(48u, maxColWidthFromLayout)
      );

      for (auto const* opt: previewApp->get_options()) {
        if (!hasOptionNames(opt)) continue;
        result += formatOptionHelp(
          opt,
          colWidth,
          layout.lineLength,
          layout.explicitWidthConstraint
        );
        result += '\n';
      }

      if (result.ends_with('\n')) { result.pop_back(); }
      return result;
    };
}

// Subcommand names take precedence over positional input interpretation;
// bare invocations fall through to the encode workflow unchanged.
auto registerPreviewSubcommand(CLI::App& app, CmdParseResult& result) -> CLI::App* {
  auto* sub = app.add_subcommand(
    "preview",
    "compare an original video with its encoded output side by side"
  );
  // Native help flag: CallForHelp is thrown only while parsing the preview
  // subcommand (the parent app cleared its help flag).
  sub->set_help_flag("-h,--help", "show preview help");
  auto const options = std::tuple{
    opt("original", &result.previewOriginal, "original video path", cfg::Required{}),
    opt("encoded", &result.previewEncoded, "encoded video path", cfg::Expected{0, 1}),
    opt(
      "--output",
      &result.previewOutput,
      "output video path (default: <original-dir>/<original-stem>.preview.mp4)"
    ),
    opt(
      "--start",
      &result.previewStart,
      "manual window start in seconds",
      cfg::NonNegativeNumber{}
    ),
    opt(
      "--duration",
      &result.previewDuration,
      "manual window duration in seconds",
      cfg::NonNegativeNumber{}
    ),
    opt(
      "--no-open",
      &result.previewNoOpen,
      "do not open the result in the default player"
    ),
    // Encode flags that shape the single-input probe/window encodes; also
    // registered on the parent app so either position parses.
    opt(
      "--crf",
      &result.crf,
      "video encode quality (0-51, lower=better)",
      cfg::RequiredDefault{"28"},
      cfg::Range{0, 51}
    ),
    opt(
      "--min-vmaf",
      &result.minVmaf,
      "minimum p5-VMAF quality floor for probing (0-100)",
      cfg::OptionalDefault{"95"},
      cfg::Range{0, 100}
    ),
    opt(
      "--preset",
      &result.nvencPreset,
      "NVENC preset (p1-p7; auto picks by resolution)",
      cfg::RequiredDefault{"auto"},
      cfg::Members{"auto", "p1", "p2", "p3", "p4", "p5", "p6", "p7"}
    ),
    opt(
      "--video-codec",
      &result.videoCodec,
      "video encoder (default hevc_nvenc; libx265/libx264 on cpu)",
      cfg::DefaultValue{"hevc_nvenc"}
    ),
  };
  registerAll(sub, options);
  sub->formatter_fn(makePreviewHelpFormatter(sub));
  return sub;
}

auto registerGeneralFlags(CLI::App& app, CLI::App* general, CmdParseResult& result)
  -> CLI::Option* {
  // help/version bypass the spec table: the -hh two-tier mechanism needs the
  // help option pointer returned to the caller
  auto* helpOpt =
    app.add_flag("-h,--help", result.help, "show help; use -hh to show all options");
  app.add_flag("--version", result.version, "show version information");
  auto const options = std::tuple{
    opt(
      "-v,--verbose",
      &result.verbose,
      "echo log lines to the console (disables progress bars)"
    ),
    opt(
      "--log-json",
      &result.jsonEnabled,
      "enable NDJSON structured log output (one JSON object per line)"
    ),
    opt(
      "-F,--full-progress",
      &result.fullProgress,
      "show full progress with per-worker encoding bars and per-archive packing "
      "bars"
    ),
    opt(
      "--color",
      &result.color,
      "terminal colors: auto, always, never",
      cfg::OptionalDefault{"auto"},
      cfg::Transform{[](std::string value) {
        std::ranges::transform(value, value.begin(), [](unsigned char ch) {
          return static_cast<char>(std::tolower(ch));
        });
        return value;
      }},
      cfg::Members{"auto", "always", "never"}
    ),
    opt("-y,--yes", &result.yesToAll, "automatic yes to prompts"),
  };
  registerAll(general, options);
  return helpOpt;
}

void registerIoFlags(CLI::App* io, CmdParseResult& result) {
  constexpr auto kMaxPositionalInputs = 1000000;
  auto const options = std::tuple{
    opt(
      "-i,--input",
      &result.input,
      "input file or directory path",
      cfg::Excludes{"--inputs"}
    ),
    opt(
      "-I,--inputs",
      &result.inputs,
      "input video file paths",
      cfg::Expected{0, kMaxPositionalInputs}
    ),
    opt(
      "-o,--output",
      &result.output,
      "custom output directory path\n  aliases: + or input:// for input "
      "root, = or common:// for common root"
    ),
    opt("--state-file", &result.stateFile, "custom job state file path"),
    opt(
      "-f,--output-format",
      &result.outputFormat,
      "target format: mp4 or webp",
      cfg::OptionalDefault{"mp4"},
      cfg::Members{"mp4", "webp"}
    ),
    opt(
      "--keep",
      &result.keep,
      "preserve relative input subdirectories inside the output directory "
      "(default: flatten)"
    ),
    opt(
      "--force-conflict-handling",
      &result.forceConflictHandling,
      "same-name collisions in flat output: y=auto-rename, n=allow "
      "duplicates",
      cfg::OptionalDefault{"y"},
      cfg::CheckedTransformer{{{"y", "y"}, {"Y", "y"}, {"n", "n"}, {"N", "n"}}}
    ),
    opt(
      "-s,--folder-summary",
      &result.folderSummary,
      "enable picture-mode folder summary images in flat packs"
    ),
    opt("-r,--recursive", &result.recursive, "enable recursively search"),
  };
  registerAll(io, options);
  auto const positional = std::tuple{
    opt(
      "input-paths",
      &result.positionalInputs,
      "input file or directory paths (alternative to -i/-I)",
      cfg::Expected{0, kMaxPositionalInputs},
      cfg::Excludes{"--input"},
      cfg::Excludes{"--inputs"}
    ),
  };
  registerAll(io, positional);
}

void registerProcessingFlags(CLI::App* processing, CmdParseResult& result) {
  auto const options = std::tuple{
    opt(
      "-t,--type",
      &result.processType,
      "process type: video(vid)|picture(pic)",
      cfg::OptionalDefault{"video"},
      cfg::CheckedTransformer{{"vid", "video"}, {"pic", "picture"}}
    ),
    opt(
      "-j,--jobs",
      &result.maxJobs,
      "max parallel jobs (>=1)",
      cfg::OptionalDefault{"10"},
      cfg::PositiveNumber{}
    ),
    opt(
      "--resume",
      &result.resume,
      "require matching previous job state; error if missing or mismatched",
      cfg::Excludes{"--restart"}
    ),
    opt("--restart", &result.restart, "ignore previous job state and start a fresh run"),
    opt("-x,--ffmpeg-path", &result.ffmpegPath, "custom ffmpeg install path"),
    opt(
      "-c,--compress",
      &result.compress,
      "enable JPEG compression during picture processing"
    ),
    opt(
      "-q,--image-quality",
      &result.imageQuality,
      "JPEG compression quality (2-31, lower=better)",
      cfg::RequiredDefault{"2"},
      cfg::Range{2, 31},
      cfg::Needs{"--compress"}
    ),
    opt(
      "--crf",
      &result.crf,
      "video encode quality (0-51, lower=better)",
      cfg::RequiredDefault{"28"},
      cfg::Range{0, 51}
    ),
    opt(
      "--min-vmaf",
      &result.minVmaf,
      "minimum p5-VMAF quality floor for probing (0-100)",
      cfg::OptionalDefault{"95"},
      cfg::Range{0, 100}
    ),
    opt(
      "--dry-run",
      &result.dryRun,
      "probe and print the encoding plan, then exit without encoding",
      cfg::Excludes{"--crf"}
    ),
    opt(
      "--preset",
      &result.nvencPreset,
      "NVENC preset (p1-p7; auto picks by resolution)",
      cfg::RequiredDefault{"auto"},
      cfg::Members{"auto", "p1", "p2", "p3", "p4", "p5", "p6", "p7"}
    ),
    opt(
      "--video-codec",
      &result.videoCodec,
      "video encoder (default hevc_nvenc; libx265/libx264 on cpu)",
      cfg::DefaultValue{"hevc_nvenc"}
    ),
  };
  registerAll(processing, options);
}

void registerFileOpFlags(CLI::App* fileop, CmdParseResult& result) {
  auto const options = std::tuple{
    opt(
      "-p,--pack",
      &result.pack,
      "pack encoded video outputs into zip files",
      cfg::Excludes{"--pack-only"}
    ),
    opt(
      "-z,--pack-only",
      &result.packOnly,
      "pack only: zip all files in input directory"
    ),
    opt("-w,--overwrite", &result.overwrite, "overwrite existing files without prompt"),
  };
  registerAll(fileop, options);
}

auto parseAndPopulate(
  CLI::App& app,
  int argc,
  char* argv[],
  CLI::App* previewSub,
  CmdParseResult& result
) -> CmdParseResult& {
  // result is the SAME object the options were bound to at registration time
  // (bound callbacks write into it during parse).
  try {
    app.parse(argc, argv);
    result.helpText = app.help();
    if (app.got_subcommand(previewSub)) { result.preview = true; }
  } catch (CLI::CallForHelp const&) {
    // Only the preview subcommand has a native help flag (the parent app
    // cleared its own), so the help text always comes from the subcommand.
    result.help = true;
    result.helpText = previewSub->help();
  } catch (CLI::ParseError const& ex) {
    result.error = ex.what();
    result.helpText = app.help();
  }
  return result;
}

}  // namespace

auto commandLineInit(int argc, char* argv[], std::string const& introLine)
  -> CmdParseResult {
  auto result = CmdParseResult{};
  auto app = CLI::App{"Allowed options"};
  app.description(introLine);
  app.set_help_flag("");

  // Create option groups
  auto* general = app.add_option_group("General", "General options");
  auto* io = app.add_option_group("IO", "Input/Output options");
  auto* processing = app.add_option_group("Processing", "Processing options");
  auto* fileop = app.add_option_group("FileOp", "File operation options");

  // Register help and version on app (not in any group), then the rest on
  // the general group
  auto const helpOpt = registerGeneralFlags(app, general, result);

  registerIoFlags(io, result);

  auto const previewSub = registerPreviewSubcommand(app, result);

  registerProcessingFlags(processing, result);
  registerFileOpFlags(fileop, result);

  // Configure formatter (static storage: kAdvancedLongNames outlives the lambda)
  app.formatter_fn(
    makeHelpFormatter(general, io, processing, fileop, helpOpt, kAdvancedLongNames)
  );

  // ── Parse and populate ──
  return parseAndPopulate(app, argc, argv, previewSub, result);
}
