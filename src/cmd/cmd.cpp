#include "cmd/cmd.h"

#include "cmd/config_store.h"
#include "cmd/option_specs.h"

#include "infra/console_width.h"
#include "infra/env.h"
#include "infra/terminal.h"

#include <CLI/CLI.hpp>

#include <array>
#include <algorithm>
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
};

auto resolveHelpTextLayout() -> HelpTextLayout {
  auto lineLength = 120u;
  if (
    auto const columns = processenv::readNonEmptyEnvVar("COLUMNS"); columns.has_value()
  ) {
    if (
      auto const override = consolewidth::parsePositiveColumnCount(columns->c_str());
      override.has_value()
    ) {
      lineLength = static_cast<unsigned>(
        std::clamp(override.value(), std::size_t{40}, std::size_t{120})
      );
    }
  }

  return {
    .lineLength = lineLength,
    .minDescriptionLength = lineLength / 2,
  };
}

// ── formatter_fn helpers (no color — plain text, Phase 20 adds color) ──

auto formatOptionName(CLI::Option const* opt) -> std::string {
  if (opt->get_positional()) { return opt->get_name(true); }

  auto const hasLongName = [&lnames = opt->get_lnames()](std::string const& name) {
    return std::ranges::find(lnames, name) != lnames.end();
  };

  auto names = std::string{};
  auto const& lnames = opt->get_lnames();
  auto const& snames = opt->get_snames();
  if (!lnames.empty()) {
    auto first = true;
    for (auto const& ln: lnames) {
      // collapse a registered negation pair (--pack, --no-pack) into --[no-]pack
      if (ln.starts_with("no-") && hasLongName(ln.substr(3))) { continue; }
      if (!first) names += ',';
      first = false;
      names += hasLongName("no-" + ln) ? "--[no-]" + ln : "--" + ln;
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

unsigned descriptionWrapWidth(unsigned descriptionColumn, unsigned lineLength) {
  return descriptionColumn < lineLength ? lineLength - descriptionColumn : 1u;
}

auto formatDefaultStr(CLI::Option const* opt) -> std::string {
  auto const defaultStr = opt->get_default_str();
  return defaultStr.empty() ? std::string{} : " (=" + defaultStr + ")";
}

auto formatOptionHelp(CLI::Option const* opt, unsigned colWidth, unsigned lineLength)
  -> std::string {
  auto const nameStr = formatOptionName(opt);

  // Column 1 = name + (=default); a type column is intentionally never
  // rendered, so binding/validator type names cannot leak into help.
  auto const defaultText = formatDefaultStr(opt);
  auto styledDefaultText = std::string{};
  if (!defaultText.empty()) {
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

  // Pad the full first column (name + default) to colWidth for alignment.
  // Layout math uses plain-text widths only, so wrapping is identical across
  // color modes (ANSI styling never influences line breaks).
  auto const firstCol = nameStr + defaultText;
  auto const gap = firstCol.size() < colWidth ? colWidth - firstCol.size() : 2u;
  auto const descriptionColumn = static_cast<unsigned>(2 + firstCol.size() + gap);
  auto const indent = std::string(descriptionColumn, ' ');
  auto const descriptionWidth = descriptionWrapWidth(descriptionColumn, lineLength);

  auto const& description = opt->get_description();
  auto const wrappedDescription =
    wrapDescription(description, descriptionWidth, descriptionWidth);
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

// Git-style auto-fit: descriptions start right after the widest first column
// plus a fixed 3-space gap; the COLUMNS-derived cap still bounds narrow
// terminals so lines never overflow the configured width.
unsigned computeColumnWidth(unsigned widestFirstColumn, HelpTextLayout const& layout) {
  auto const maxColWidthFromLayout = layout.lineLength > layout.minDescriptionLength + 2
    ? layout.lineLength - layout.minDescriptionLength - 2
    : 1u;
  return std::min(widestFirstColumn + 3u, maxColWidthFromLayout);
}

// Git-style commands section: one row per real subcommand, description
// aligned by the same auto-fit rule (column helper, cap, gap fallback) as the
// option tables. Callers pass the registered subcommand apps explicitly:
// CLI11's get_subcommands() mixes in the option groups (they are App
// subcommands too) and its no-arg overload returns only the subcommands
// parsed from the current command line.
auto formatCommandsSection(
  std::span<CLI::App const* const> subcommands,
  HelpTextLayout const& layout
) -> std::string {
  if (subcommands.empty()) { return {}; }

  auto widest = 0u;
  for (auto const* sub: subcommands) {
    widest = std::max(widest, static_cast<unsigned>(sub->get_name().size()));
  }
  auto const colWidth = computeColumnWidth(widest, layout);

  auto result = terminal::styledText(
    terminal::Stream::Stdout,
    terminal::MessageKind::OptionGroup,
    "encro commands"
  );
  result += ":\n";
  for (auto const* sub: subcommands) {
    auto const name = sub->get_name();
    auto const nameWidth = static_cast<unsigned>(name.size());
    auto const gap = nameWidth < colWidth ? colWidth - nameWidth : 2u;
    auto const descriptionColumn = 2u + nameWidth + gap;
    auto const descriptionWidth =
      descriptionWrapWidth(descriptionColumn, layout.lineLength);
    auto const wrappedDescription =
      wrapDescription(sub->get_description(), descriptionWidth, descriptionWidth);
    auto const indent = std::string(descriptionColumn, ' ');
    for (auto lineNum = 0u; lineNum < wrappedDescription.size(); ++lineNum) {
      if (lineNum == 0) {
        result += std::format(
          "  {}{:{}}{}\n",
          terminal::styledText(
            terminal::Stream::Stdout,
            terminal::MessageKind::OptionName,
            name
          ),
          "",
          gap,
          terminal::styledText(
            terminal::Stream::Stdout,
            terminal::MessageKind::OptionDesc,
            wrappedDescription[lineNum]
          )
        );
      } else {
        result += std::format(
          "{}{}\n",
          indent,
          terminal::styledText(
            terminal::Stream::Stdout,
            terminal::MessageKind::OptionDesc,
            wrappedDescription[lineNum]
          )
        );
      }
    }
  }
  return result;
}

// Description line + the "Usage" section shared by the main and subcommand
// help formatters.
auto formatHelpPreamble(
  CLI::App const* app,
  std::span<std::string_view const> usageLines,
  HelpTextLayout const& layout
) -> std::string {
  auto result = std::string{};
  auto const desc = app->get_description();
  if (!desc.empty()) {
    result +=
      terminal::styledText(terminal::Stream::Stdout, terminal::MessageKind::Usage, desc);
    result += "\n\n";
  }
  result += formatHelpSection("Usage", usageLines, layout.lineLength);
  result += '\n';
  return result;
}

auto makeHelpFormatter(
  CLI::App const* general,
  CLI::App const* io,
  CLI::App const* processing,
  CLI::App const* fileop,
  CLI::Option const* helpOpt,
  std::span<std::string_view const> advancedLongNames,
  std::span<CLI::App const* const> subcommands
) -> auto {
  return  //
    [general, io, processing, fileop, helpOpt, advancedLongNames, subcommands](
      CLI::App const* app_ptr,
      std::string /*prev*/
      ,  // NOLINT(performance-unnecessary-value-param): CLI11 formatter callback signature is fixed
      CLI::AppFormatMode /*mode*/
    ) -> std::string {
      // Subcommand synopsis lines stay out of the usage block: the commands
      // section below carries them with one-line descriptions.
      constexpr auto usageLines = std::array{
        "encro [<input>... | -i <input> | -I <file>...] [-o <output>] [-f mp4|webp] [-r] [-j <n>] [-p] [--resume|--restart]"sv,
        "encro -t picture <input> [-c [-q <n>]] [-s] [-p]"sv,
        "encro -z <input> [-o <output>]"sv,
        "encro -h | -hh | --version"sv,
      };
      auto const fullTier = helpOpt->count() >= 2;
      constexpr auto hintLine = "Run 'encro -hh' to view all options."sv;

      auto const layout = resolveHelpTextLayout();
      auto result = formatHelpPreamble(app_ptr, usageLines, layout);
      result += formatCommandsSection(subcommands, layout);
      auto const groupIter = std::array{general, io, processing, fileop};
      auto const maxColumnLen = computeMaxColumnLen(
        general,
        std::span{groupIter},
        app_ptr,
        advancedLongNames,
        fullTier
      );
      auto const colWidth = computeColumnWidth(maxColumnLen, layout);

      for (auto const* group: groupIter) {
        result += formatGroupHeader(group->get_description());
        for (auto const* opt: visibleOptionsOf(group, general, app_ptr)) {
          if (!hasOptionNames(opt)) continue;
          if (!fullTier && isAdvancedOption(opt, advancedLongNames)) continue;
          result += formatOptionHelp(opt, colWidth, layout.lineLength);
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

// Subcommand help: rendered from the subcommand's own option definitions with
// the same style helpers as the main help. It deliberately does not reuse
// makeHelpFormatter, which renders the whole main option table (captured
// parent group pointers).
auto makeSubcommandHelpFormatter(
  CLI::App const* subApp,
  std::span<std::string_view const> usageLines
) -> auto {
  return  //
    [subApp, usageLines](
      CLI::App const* appPtr,
      std::string /*prev*/
      ,  // NOLINT(performance-unnecessary-value-param): CLI11 formatter callback signature is fixed
      CLI::AppFormatMode /*mode*/
    ) -> std::string {
      auto const layout = resolveHelpTextLayout();
      auto result = formatHelpPreamble(appPtr, usageLines, layout);

      // Same column-width logic as the main formatter, over the subcommand's
      // own options only (general=nullptr keeps visibleOptionsOf from
      // double-adding the app-level options).
      auto const maxColumnLen =
        computeMaxColumnLen(nullptr, std::span{&subApp, 1}, appPtr, {}, true);
      auto const colWidth = computeColumnWidth(maxColumnLen, layout);

      for (auto const* opt: subApp->get_options()) {
        if (!hasOptionNames(opt)) continue;
        result += formatOptionHelp(opt, colWidth, layout.lineLength);
        result += '\n';
      }

      if (result.ends_with('\n')) { result.pop_back(); }
      return result;
    };
}

// Subcommand names take precedence over positional input interpretation;
// bare invocations fall through to the encode workflow unchanged.
constexpr auto kPreviewUsageLines = std::array{
  "encro preview <original> [<encoded>] [--start <s>] [--duration <s>] [--output <path>] [--no-open]"sv,
};

constexpr auto kConfigUsageLines = std::array{
  "encro config <list|get <key>|set <key> <value>|unset <key>|path>"sv,
};

constexpr auto kCompletionUsageLines = std::array{
  "encro completion <powershell|bash> [--install | --uninstall]"sv,
};

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
  sub->formatter_fn(makeSubcommandHelpFormatter(sub, kPreviewUsageLines));
  return sub;
}

// Persistent user-level configuration (spec: user-config). Actions are
// mutually exclusive; bare `encro config` shows the subcommand help.
auto registerConfigSubcommand(CLI::App& app, CmdParseResult& result) -> CLI::App* {
  auto* sub =
    app.add_subcommand("config", "inspect and persist user-level configuration defaults");
  sub->set_help_flag("-h,--help", "show config help");
  auto const options = std::tuple{
    opt(
      "--list",
      &result.configList,
      "show every configurable key with its value and source",
      cfg::Excludes{"--get"},
      cfg::Excludes{"--set"},
      cfg::Excludes{"--unset"},
      cfg::Excludes{"--path"}
    ),
    opt(
      "--get",
      &result.configGet,
      "print the effective value of one key",
      cfg::Excludes{"--list"},
      cfg::Excludes{"--set"},
      cfg::Excludes{"--unset"},
      cfg::Excludes{"--path"}
    ),
    opt(
      "--set",
      &result.configSet,
      "validate and persist a value: --set <key> <value>",
      cfg::Expected{2, 2},
      cfg::Excludes{"--list"},
      cfg::Excludes{"--get"},
      cfg::Excludes{"--unset"},
      cfg::Excludes{"--path"}
    ),
    opt(
      "--unset",
      &result.configUnset,
      "remove a persisted key (falls back to the built-in default)",
      cfg::Excludes{"--list"},
      cfg::Excludes{"--get"},
      cfg::Excludes{"--set"},
      cfg::Excludes{"--path"}
    ),
    opt(
      "--path",
      &result.configPath,
      "print the resolved config file location",
      cfg::Excludes{"--list"},
      cfg::Excludes{"--get"},
      cfg::Excludes{"--set"},
      cfg::Excludes{"--unset"}
    ),
  };
  registerAll(sub, options);
  sub->formatter_fn(makeSubcommandHelpFormatter(sub, kConfigUsageLines));
  return sub;
}

// Completion scripts for supported shells; install/uninstall are mutually
// exclusive; bare `encro completion` shows the subcommand help.
auto registerCompletionSubcommand(CLI::App& app, CmdParseResult& result) -> CLI::App* {
  auto* sub = app.add_subcommand(
    "completion",
    "print, install, or uninstall shell completion scripts"
  );
  sub->set_help_flag("-h,--help", "show completion help");
  auto const options = std::tuple{
    opt(
      "shell",
      &result.completionShell,
      "target shell: powershell or bash",
      cfg::Members{"bash", "powershell"}
    ),
    opt(
      "--install",
      &result.completionInstall,
      "install the completion script for the shell",
      cfg::Excludes{"--uninstall"}
    ),
    opt(
      "--uninstall",
      &result.completionUninstall,
      "remove the installed completion script",
      cfg::Excludes{"--install"}
    ),
  };
  registerAll(sub, options);
  sub->formatter_fn(makeSubcommandHelpFormatter(sub, kCompletionUsageLines));
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
      cfg::ConfigKey{"color"},
      cfg::Transform{[](std::string value) {
        std::ranges::transform(value, value.begin(), [](unsigned char ch) {
          return static_cast<char>(std::tolower(ch));
        });
        return value;
      }},
      cfg::Members{"auto", "always", "never"}
    ),
    opt(
      "-y,--yes,--no-yes{false}",
      &result.yesToAll,
      "automatic yes to prompts",
      cfg::ConfigKey{"yes"}
    ),
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
      cfg::ConfigKey{"output-format"},
      cfg::Members{"mp4", "webp"}
    ),
    opt(
      "--keep,--no-keep{false}",
      &result.keep,
      "preserve relative input subdirectories inside the output directory "
      "(default: flatten)",
      cfg::ConfigKey{"keep"}
    ),
    opt(
      "--force-conflict-handling",
      &result.forceConflictHandling,
      "same-name collisions in flat output: y=auto-rename, n=allow "
      "duplicates",
      cfg::OptionalDefault{"y"},
      cfg::ConfigKey{"force-conflict-handling"},
      cfg::CheckedTransformer{{{"y", "y"}, {"Y", "y"}, {"n", "n"}, {"N", "n"}}}
    ),
    opt(
      "-s,--folder-summary,--no-folder-summary{false}",
      &result.folderSummary,
      "enable picture-mode folder summary images in flat packs",
      cfg::ConfigKey{"folder-summary"}
    ),
    opt(
      "-r,--recursive,--no-recursive{false}",
      &result.recursive,
      "enable recursively search",
      cfg::ConfigKey{"recursive"}
    ),
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

void registerProcessingFlags(
  CLI::App* processing,
  CmdParseResult& result
)  // NOLINT(readability-function-size): declarative option table
{
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
      cfg::ConfigKey{"jobs"},
      cfg::PositiveNumber{}
    ),
    opt(
      "--resume",
      &result.resume,
      "require matching previous job state; error if missing or mismatched",
      cfg::Excludes{"--restart"}
    ),
    opt("--restart", &result.restart, "ignore previous job state and start a fresh run"),
    opt(
      "-x,--ffmpeg-path",
      &result.ffmpegPath,
      "custom ffmpeg install path",
      cfg::ConfigKey{"ffmpeg-path"}
    ),
    opt(
      "-c,--compress,--no-compress{false}",
      &result.compress,
      "enable JPEG compression during picture processing",
      cfg::ConfigKey{"compress"}
    ),
    opt(
      "-q,--image-quality",
      &result.imageQuality,
      "JPEG compression quality (2-31, lower=better)",
      cfg::RequiredDefault{"2"},
      cfg::ConfigKey{"image-quality"},
      cfg::Range{2, 31},
      cfg::Needs{"--compress"}
    ),
    opt(
      "--crf",
      &result.crf,
      "video encode quality (0-51, lower=better)",
      cfg::RequiredDefault{"28"},
      cfg::ConfigKey{"crf"},
      cfg::Range{0, 51}
    ),
    opt(
      "--min-vmaf",
      &result.minVmaf,
      "minimum p5-VMAF quality floor for probing (0-100)",
      cfg::OptionalDefault{"95"},
      cfg::ConfigKey{"min-vmaf"},
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
      cfg::ConfigKey{"preset"},
      cfg::Members{"auto", "p1", "p2", "p3", "p4", "p5", "p6", "p7"}
    ),
    opt(
      "--video-codec",
      &result.videoCodec,
      "video encoder (default hevc_nvenc; libx265/libx264 on cpu)",
      cfg::DefaultValue{"hevc_nvenc"},
      cfg::ConfigKey{"video-codec"}
    ),
  };
  registerAll(processing, options);
}

void registerFileOpFlags(CLI::App* fileop, CmdParseResult& result) {
  auto const options = std::tuple{
    opt(
      "-p,--pack,--no-pack{false}",
      &result.pack,
      "pack encoded video outputs into zip files",
      cfg::ConfigKey{"pack"},
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

// Loads the user config and applies each stored value as a forced option
// default (design D1). Returns a load-error message, or nullopt.
auto injectConfigDefaults(CLI::App& app) -> std::optional<std::string> {
  auto const configPath = configstore::resolveConfigPath();
  auto const loaded = configstore::load(configPath);
  if (loaded.error) { return loaded.error; }

  configstore::warnUnknownKeys(loaded, configPath);
  for (auto const& [key, value]: loaded.values) {
    if (auto* opt = app.get_option_no_throw("--" + key)) {
      opt->default_str(value);
      opt->force_callback();
    }
    // Named subcommands may register their own copy of a config-key option
    // (preview's encode twins); get_option lookup only descends into nameless
    // groups, so sync the twins here. Display only, never force_callback: the
    // main option's forced callback already writes the binding (registration
    // order), and a forced subcommand callback would run after an explicit
    // main value and clobber it. Note: the no-arg get_subcommands() returns
    // parsed subcommands only (empty pre-parse); the filter overload returns
    // every registered subcommand.
    for (auto* subc: app.get_subcommands([](CLI::App*) { return true; })) {
      if (subc->get_name().empty()) { continue; }  // option groups: handled above
      if (auto* subOpt = subc->get_option_no_throw("--" + key)) {
        subOpt->default_str(value);
      }
    }
  }
  return std::nullopt;
}

auto buildAndParse(
  int argc,
  char* argv[],
  std::string const& introLine,
  bool injectConfig
) -> CmdParseResult {
  auto result = CmdParseResult{};
  auto tree = buildAppTree(result, introLine, injectConfig);
  if (result.error.has_value()) { return result; }

  // result is the SAME object the options were bound to at registration time
  // (bound callbacks write into it during parse).
  try {
    tree.app->parse(argc, argv);
    result.helpText = tree.app->help();
    if (tree.app->got_subcommand(tree.previewSub)) { result.preview = true; }
    if (tree.app->got_subcommand(tree.configSub)) {
      result.config = true;
      result.helpText = tree.configSub->help();
    }
    if (tree.app->got_subcommand(tree.completionSub)) {
      result.completion = true;
      result.helpText = tree.completionSub->help();
    }
  } catch (CLI::CallForHelp const&) {
    // The preview/config/completion subcommands carry the native help flags
    // (the parent app cleared its own), so the help text comes from whichever
    // matched.
    result.help = true;
    if (tree.app->got_subcommand(tree.completionSub)) {
      result.helpText = tree.completionSub->help();
    } else if (tree.app->got_subcommand(tree.configSub)) {
      result.helpText = tree.configSub->help();
    } else {
      result.helpText = tree.previewSub->help();
    }
  } catch (CLI::ParseError const& ex) {
    result.error = ex.what();
    result.helpText = tree.app->help();
  }
  return result;
}

}  // namespace

auto buildAppTree(CmdParseResult& result, std::string const& introLine, bool injectConfig)
  -> AppTree {
  // Leaked on purpose (never freed): the config-command registry keeps
  // pointers to the registered options (design D3), so the app must outlive
  // this call. One small allocation per parse keeps them process-lifetime.
  auto* app = new CLI::App{"Allowed options"};
  app->description(introLine);
  app->set_help_flag("");

  // Create option groups
  auto* general = app->add_option_group("General", "General options");
  auto* io = app->add_option_group("IO", "Input/Output options");
  auto* processing = app->add_option_group("Processing", "Processing options");
  auto* fileop = app->add_option_group("FileOp", "File operation options");

  // Register help and version on app (not in any group), then the rest on
  // the general group
  auto const helpOpt = registerGeneralFlags(*app, general, result);

  registerIoFlags(io, result);

  // Value options must be registered before the preview subcommand's
  // encode-shaping twins (same CmdParseResult bindings): callbacks run in
  // registration order, so a config-injected main default is applied first
  // and an explicit preview --crf then overwrites it.
  registerProcessingFlags(processing, result);
  registerFileOpFlags(fileop, result);

  auto* previewSub = registerPreviewSubcommand(*app, result);
  auto* configSub = registerConfigSubcommand(*app, result);
  auto* completionSub = registerCompletionSubcommand(*app, result);
  // Option groups are CLI11 subcommands too, so the commands section is given
  // the real subcommand apps explicitly. The array is leaked with the app:
  // the formatter lambda captures the span by value, while parse and help
  // rendering happen after this call returns.
  auto* const commandSubs =
    new std::array<CLI::App const*, 3>{previewSub, configSub, completionSub};

  // Configure formatter (static storage: kAdvancedLongNames outlives the lambda)
  app->formatter_fn(makeHelpFormatter(
    general,
    io,
    processing,
    fileop,
    helpOpt,
    kAdvancedLongNames,
    std::span{*commandSubs}
  ));

  // ── Config injection (design D1): config values become forced option
  // defaults, so CLI values win, validators run on applied defaults, and the
  // help (=default) display shows effective defaults.
  if (injectConfig) {
    if (auto const error = injectConfigDefaults(*app); error.has_value()) {
      result.error = *error;
    }
  }
  return AppTree{app, previewSub, configSub, completionSub};
}

auto commandLineInit(int argc, char* argv[], std::string const& introLine)
  -> CmdParseResult {
  // Probe parse without config injection: config-subcommand actions must
  // operate on stores holding invalid values (e.g. `config unset` of a bad
  // key), and pure CLI errors surface unchanged. Every other path re-parses
  // with injection so option defaults and the help (=default) display reflect
  // the config.
  auto probe = buildAndParse(argc, argv, introLine, false);
  // completion joins config here: emission never reads the stored config, so
  // a corrupt config file must not fail it (add-shell-completion spec).
  if (probe.error.has_value() || probe.config || probe.completion) { return probe; }
  return buildAndParse(argc, argv, introLine, true);
}
