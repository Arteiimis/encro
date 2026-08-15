#include "cmd/cmd.h"

#include "infra/env.h"
#include "infra/terminal.h"

#include <CLI/CLI.hpp>

#include <array>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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

auto countLeadingWhitespace(std::string_view text) -> std::size_t {
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

  auto typeStr = std::string{};
  if (opt->get_expected_min() > 0 && !opt->get_type_name().empty()) {
    auto const typeName = opt->get_type_name();
    if (typeName != "TEXT"sv && typeName != "text"sv) { typeStr = " " + typeName; }
  }

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

  // Pad the full first column (name + type + default) to colWidth for alignment
  auto const firstCol = nameStr + typeStr + defaultText;
  auto const gap = firstCol.size() < colWidth ? colWidth - firstCol.size() : 2u;
  auto const displayDescriptionColumn = static_cast<unsigned>(2 + firstCol.size() + gap);
  auto const renderedDescriptionColumn = static_cast<unsigned>(
    2 + coloredName.size() + typeStr.size() + styledDefaultText.size() + gap
  );
  auto const indent = std::string(displayDescriptionColumn, ' ');
  auto const firstLineDescriptionColumn =
    explicitWidthConstraint ? renderedDescriptionColumn : displayDescriptionColumn;
  auto const firstLineWidth = firstLineDescriptionColumn < lineLength
    ? lineLength - firstLineDescriptionColumn
    : 1u;
  auto const continuationWidth =
    displayDescriptionColumn < lineLength ? lineLength - displayDescriptionColumn : 1u;

  auto const description = opt->get_description();
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
        "  {}{}{}{:<{}}{}\n",
        coloredName,
        typeStr,
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

auto hasOptionNames(CLI::Option const* opt) -> bool {
  return opt->nonpositional() || opt->get_positional();
}

auto isAdvancedOption(
  CLI::Option const* opt,
  std::span<std::string_view const> advancedLongNames
) -> bool {
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

auto buildPreviewHelpText() -> std::string {
  auto result = std::string{};
  result +=
    "encro preview: compare an original video with its encoded output side by side\n\n";
  result += "Usage:\n";
  result += "  encro preview <original> <encoded> [options]\n\n";
  result += "Positional:\n";
  result += "  original, encoded   video files to compare (original vs encoded)\n\n";
  result += "Options:\n";
  result += "  --output <path>     output video path (default: "
            "<original-dir>/<original-stem>.preview.mp4)\n";
  result += "  --start <seconds>   manual window start (skips sampling and scoring)\n";
  result += "  --duration <seconds> manual window duration (clamped at the video end)\n";
  result += "  --no-open           do not open the result in the default player\n";
  result += "  -h, --help          show this help\n";
  return result;
}

auto formatTypeStr(CLI::Option const* opt) -> std::string {
  if (opt->get_expected_min() > 0 && !opt->get_type_name().empty()) {
    auto const typeName = opt->get_type_name();
    if (typeName != "TEXT"sv && typeName != "text"sv) { return " " + typeName; }
  }
  return {};
}

auto formatDefaultStr(CLI::Option const* opt) -> std::string {
  auto const defaultStr = opt->get_default_str();
  return defaultStr.empty() ? std::string{} : " (=" + defaultStr + ")";
}

// Max column width across visible options (name + type + default).
auto computeMaxColumnLen(
  CLI::App const* general,
  std::span<CLI::App const* const> groups,
  CLI::App const* appPtr,
  std::span<std::string_view const> advancedLongNames,
  bool fullTier
) -> unsigned {
  auto maxLen = 0u;
  for (auto const* group: groups) {
    for (auto const* opt: visibleOptionsOf(group, general, appPtr)) {
      if (!hasOptionNames(opt)) continue;
      if (!fullTier && isAdvancedOption(opt, advancedLongNames)) continue;
      auto const nameStr = formatOptionName(opt);
      maxLen = std::max(
        maxLen,
        static_cast<unsigned>(
          nameStr.size() + formatTypeStr(opt).size() + formatDefaultStr(opt).size()
        )
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
      std::string /*prev*/,
      CLI::AppFormatMode /*mode*/
    ) -> std::string {
      constexpr auto usageLines = std::array{
        "encro [<input>... | -i <input> | -I <file>...] [-o <output>] [-f mp4|webp] [-r] [-j <n>] [-p] [--resume|--restart]"sv,
        "encro -t picture <input> [-c [-q <n>]] [-s] [-p]"sv,
        "encro -z <input> [-o <output>]"sv,
        "encro preview <original> <encoded> [--start <s>] [--duration <s>] [--output <path>] [--no-open]"sv,
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

}  // namespace

enum class CmdFlagKind {
  Bool,
  String,
  Int,
  SizeT,
  VecString
};

struct CmdFlagDef {
  std::string_view name;  // CLI11-native format: "-v,--verbose" or "--version"
  CmdFlagKind kind;       // Bool | String | Int | SizeT | VecString
  std::string_view description;
  std::string_view
    defaultValue;  // "" → no default (expectedMin=1); non-empty → has default (expectedMin=0)
  int expectedMax;            // 0=flag, 1=single value, >1=multi-value upper bound
  std::string_view excludes;  // flag name this excludes ("" = none); CLI11 ->excludes()
  std::string_view excludesDesc;    // custom error message for the exclusion
  bool advanced = false;            // hidden from brief (-h) help
  std::string_view defaultDisplay;  // "" = none; shown as (=value) in help
};

// ── General flags (7) ──  help/version on app, rest on general group ──
constexpr auto GeneralFlags = std::array{
  CmdFlagDef{
    .name = "-h,--help",
    .kind = CmdFlagKind::Bool,
    .description = "show help; use -hh to show all options",
    .defaultValue = "",
    .expectedMax = 0
  },
  CmdFlagDef{
    .name = "--version",
    .kind = CmdFlagKind::Bool,
    .description = "show version information",
    .defaultValue = "",
    .expectedMax = 0
  },
  CmdFlagDef{
    .name = "-v,--verbose",
    .kind = CmdFlagKind::Bool,
    .description = "echo log lines to the console (disables progress bars)",
    .defaultValue = "",
    .expectedMax = 0,
    .advanced = true
  },
  CmdFlagDef{
    .name = "--log-json",
    .kind = CmdFlagKind::Bool,
    .description = "enable NDJSON structured log output (one JSON object per line)",
    .defaultValue = "",
    .expectedMax = 0,
    .advanced = true
  },
  CmdFlagDef{
    .name = "-F,--full-progress",
    .kind = CmdFlagKind::Bool,
    .description =
      "show full progress with per-worker encoding bars and per-archive packing bars",
    .defaultValue = "",
    .expectedMax = 0,
    .advanced = true
  },
  CmdFlagDef{
    .name = "--color",
    .kind = CmdFlagKind::String,
    .description = "terminal colors: auto, always, never",
    .defaultValue = "auto",
    .expectedMax = 1,
    .advanced = true
  },
  CmdFlagDef{
    .name = "-y,--yes",
    .kind = CmdFlagKind::Bool,
    .description = "automatic yes to prompts",
    .defaultValue = "",
    .expectedMax = 0
  },
};

// ── Input/Output flags (9) ──
constexpr auto IOFrags = std::array{
  CmdFlagDef{
    .name = "-i,--input",
    .kind = CmdFlagKind::String,
    .description = "input file or directory path",
    .defaultValue = "",
    .expectedMax = 1,
    .excludes = "-I,--inputs",
    .excludesDesc =
      "Use -i for a single input path, or -I for multiple input paths — not both."
  },
  CmdFlagDef{
    .name = "-I,--inputs",
    .kind = CmdFlagKind::VecString,
    .description = "input video file paths",
    .defaultValue = "",
    .expectedMax = 1000000,
    .advanced = true
  },
  CmdFlagDef{
    .name = "-o,--output",
    .kind = CmdFlagKind::String,
    .description =
      "custom output directory path\n  aliases: + or input:// for input root, = or "
      "common:// for common root",
    .defaultValue = "",
    .expectedMax = 1
  },
  CmdFlagDef{
    .name = "--state-file",
    .kind = CmdFlagKind::String,
    .description = "custom job state file path",
    .defaultValue = "",
    .expectedMax = 1,
    .advanced = true
  },
  CmdFlagDef{
    .name = "-f,--output-format",
    .kind = CmdFlagKind::String,
    .description = "target format: mp4 or webp",
    .defaultValue = "mp4",
    .expectedMax = 1
  },
  CmdFlagDef{
    .name = "--keep",
    .kind = CmdFlagKind::Bool,
    .description = "preserve relative input subdirectories inside the output directory "
                   "(default: flatten)",
    .defaultValue = "",
    .expectedMax = 0
  },
  CmdFlagDef{
    .name = "--force-conflict-handling",
    .kind = CmdFlagKind::String,
    .description =
      "same-name collisions in flat output: y=auto-rename, n=allow duplicates",
    .defaultValue = "y",
    .expectedMax = 1,
    .advanced = true
  },
  CmdFlagDef{
    .name = "-s,--folder-summary",
    .kind = CmdFlagKind::Bool,
    .description = "enable picture-mode folder summary images in flat packs",
    .defaultValue = "",
    .expectedMax = 0
  },
  CmdFlagDef{
    .name = "-r,--recursive",
    .kind = CmdFlagKind::Bool,
    .description = "enable recursively search",
    .defaultValue = "",
    .expectedMax = 0
  },
};

// ── Processing flags (9) ──
constexpr auto ProcessingFlags = std::array{
  CmdFlagDef{
    .name = "-t,--type",
    .kind = CmdFlagKind::String,
    .description = "process type: video(vid)|picture(pic)",
    .defaultValue = "video",
    .expectedMax = 1
  },
  CmdFlagDef{
    .name = "-j,--jobs",
    .kind = CmdFlagKind::SizeT,
    .description = "max parallel jobs (>=1)",
    .defaultValue = "10",
    .expectedMax = 1
  },
  CmdFlagDef{
    .name = "--resume",
    .kind = CmdFlagKind::Bool,
    .description = "require matching previous job state; error if missing or mismatched",
    .defaultValue = "",
    .expectedMax = 0,
    .excludes = "--restart",
    .excludesDesc =
      "--resume continues a previous job; use --restart to discard state and begin fresh."
  },
  CmdFlagDef{
    .name = "--restart",
    .kind = CmdFlagKind::Bool,
    .description = "ignore previous job state and start a fresh run",
    .defaultValue = "",
    .expectedMax = 0
  },
  CmdFlagDef{
    .name = "-x,--ffmpeg-path",
    .kind = CmdFlagKind::String,
    .description = "custom ffmpeg install path",
    .defaultValue = "",
    .expectedMax = 1,
    .advanced = true
  },
  CmdFlagDef{
    .name = "-c,--compress",
    .kind = CmdFlagKind::Bool,
    .description = "enable JPEG compression during picture processing",
    .defaultValue = "",
    .expectedMax = 0
  },
  CmdFlagDef{
    .name = "-q,--image-quality",
    .kind = CmdFlagKind::Int,
    .description = "JPEG compression quality (2-31, lower=better)",
    .defaultValue = "",
    .expectedMax = 1,
    .defaultDisplay = "2"
  },
  CmdFlagDef{
    .name = "--crf",
    .kind = CmdFlagKind::Int,
    .description = "video encode quality (0-51, lower=better)",
    .defaultValue = "",
    .expectedMax = 1,
    .defaultDisplay = "28"
  },
  CmdFlagDef{
    .name = "--min-vmaf",
    .kind = CmdFlagKind::Int,
    .description = "minimum p5-VMAF quality floor for probing (0-100)",
    .defaultValue = "95",
    .expectedMax = 1
  },
  CmdFlagDef{
    .name = "--dry-run",
    .kind = CmdFlagKind::Bool,
    .description = "probe and print the encoding plan, then exit without encoding",
    .defaultValue = "",
    .expectedMax = 0
  },
  CmdFlagDef{
    .name = "--preset",
    .kind = CmdFlagKind::String,
    .description = "NVENC preset (p1-p7; auto picks by resolution)",
    .defaultValue = "",
    .expectedMax = 1,
    .advanced = true,
    .defaultDisplay = "auto"
  },
  CmdFlagDef{
    .name = "--video-codec",
    .kind = CmdFlagKind::String,
    .description = "video encoder (default hevc_nvenc; libx265/libx264 on cpu)",
    .defaultValue = "",
    .expectedMax = 1,
    .advanced = true,
    .defaultDisplay = "hevc_nvenc"
  },
};

// ── File operation flags (3) ──
constexpr auto FileOpFlags = std::array{
  CmdFlagDef{
    .name = "-p,--pack",
    .kind = CmdFlagKind::Bool,
    .description = "pack encoded video outputs into zip files",
    .defaultValue = "",
    .expectedMax = 0,
    .excludes = "-z,--pack-only",
    .excludesDesc = "--pack encodes then packs; use --pack-only to pack without encoding."
  },
  CmdFlagDef{
    .name = "-z,--pack-only",
    .kind = CmdFlagKind::Bool,
    .description = "pack only: zip all files in input directory",
    .defaultValue = "",
    .expectedMax = 0
  },
  CmdFlagDef{
    .name = "-w,--overwrite",
    .kind = CmdFlagKind::Bool,
    .description = "overwrite existing files without prompt",
    .defaultValue = "",
    .expectedMax = 0
  },
};

struct PendingExclusion {
  CLI::Option* option;
  std::string_view targetName;
  std::string_view description;
};

auto commandLineInit(int argc, char* argv[], std::string const& introLine)
  -> CmdParseResult {
  auto app = CLI::App{"Allowed options"};
  app.description(introLine);
  app.set_help_flag("");

  // Create option groups
  auto* general = app.add_option_group("General", "General options");
  auto* io = app.add_option_group("IO", "Input/Output options");
  auto* processing = app.add_option_group("Processing", "Processing options");
  auto* fileop = app.add_option_group("FileOp", "File operation options");

  // Registry for CLI::Option* lookups after parse
  std::unordered_map<std::string_view, CLI::Option*> optRegistry;
  std::vector<PendingExclusion> pendingExcludes;

  // Data-driven flag registration helper
  auto registerFlag = [&](CmdFlagDef const& def, CLI::App* target) {
    auto const expectedMin = def.defaultValue.empty() ? 1 : 0;
    std::string const name{def.name};
    std::string const desc{def.description};
    CLI::Option* opt = nullptr;
    switch (def.kind) {
      case CmdFlagKind::Bool: opt = target->add_flag(name, desc); break;
      case CmdFlagKind::String:
        opt = target->add_option(name, desc);
        if (expectedMin == 0) {
          opt->expected(0, 1)->default_str(std::string{def.defaultValue});
        } else if (!def.defaultDisplay.empty()) {
          opt->expected(1)->default_str(std::string{def.defaultDisplay});
        } else {
          opt->expected(1);
        }
        break;
      case CmdFlagKind::Int:
        opt = target->add_option(name, desc);
        if (expectedMin == 0) {
          opt->expected(0, 1)->default_str(std::string{def.defaultValue});
        } else if (!def.defaultDisplay.empty()) {
          opt->expected(1)->default_str(std::string{def.defaultDisplay});
        } else {
          opt->expected(1);
        }
        break;
      case CmdFlagKind::SizeT:
        opt = target->add_option(name, desc);
        if (expectedMin == 0) {
          opt->expected(0, 1)->default_str(std::string{def.defaultValue});
        } else {
          opt->expected(1);
        }
        break;
      case CmdFlagKind::VecString:
        opt = target->add_option(name, desc)->expected(0, def.expectedMax);
        break;
    }
    if (opt) { optRegistry[def.name] = opt; }
    if (opt && !def.excludes.empty()) {
      pendingExcludes.emplace_back(opt, def.excludes, def.excludesDesc);
    }
  };

  // Register help and version on app (not in any group)
  for (auto const& def: std::span{GeneralFlags}.subspan(0, 2)) {
    registerFlag(def, &app);
  }

  // Register remaining GeneralFlags on general group
  for (auto const& def: std::span{GeneralFlags}.subspan(2)) {
    registerFlag(def, general);
  }

  // Register IOFrags on io group
  for (auto const& def: IOFrags) { registerFlag(def, io); }

  // Positional input paths — alternative to -i/-I; conflicts validated in buildConfig
  constexpr auto kPositionalKey = std::string_view{"<positional>"};
  constexpr auto kMaxPositionalInputs = 1000000;
  auto* positionalOpt =
    io->add_option("input-paths", "input file or directory paths (alternative to -i/-I)")
      ->expected(0, kMaxPositionalInputs);
  optRegistry[kPositionalKey] = positionalOpt;

  // ── Preview subcommand ──
  // Subcommand names take precedence over positional input interpretation;
  // bare invocations fall through to the encode workflow unchanged.
  auto* previewSub = app.add_subcommand(
    "preview",
    "compare an original video with its encoded output side by side"
  );
  previewSub->set_help_flag("");
  auto* previewOriginal = previewSub->add_option("original", "original video path");
  auto* previewEncoded = previewSub->add_option("encoded", "encoded video path");
  auto* previewOutput = previewSub->add_option(
    "--output",
    "output video path (default: <original-dir>/<original-stem>.preview.mp4)"
  );
  auto* previewStart =
    previewSub->add_option("--start", "manual window start in seconds");
  auto* previewDuration =
    previewSub->add_option("--duration", "manual window duration in seconds");
  auto* previewNoOpen =
    previewSub->add_flag("--no-open", "do not open the result in the default player");
  auto* previewHelp = previewSub->add_flag("-h,--help", "show preview help");

  // Register ProcessingFlags on processing group
  for (auto const& def: ProcessingFlags) { registerFlag(def, processing); }

  // Register FileOpFlags on fileop group
  for (auto const& def: FileOpFlags) { registerFlag(def, fileop); }

  // Resolve deferred exclusions
  for (auto const& pe: pendingExcludes) {
    auto it = optRegistry.find(pe.targetName);
    if (it != optRegistry.end()) { pe.option->excludes(it->second); }
  }

  // Collect advanced long names from the same def arrays (single source of truth).
  // -h/--help and --version are not marked advanced, so they never enter the set.
  auto advancedLongNames = std::vector<std::string_view>{};
  auto collectAdvanced = [&](auto const& defs) {
    for (auto const& def: defs) {
      if (!def.advanced) { continue; }
      auto longName = def.name;
      if (auto const comma = longName.find(','); comma != std::string_view::npos) {
        longName.remove_prefix(comma + 1);
      }
      if (longName.starts_with("--")) { longName.remove_prefix(2); }
      advancedLongNames.push_back(longName);
    }
  };
  collectAdvanced(GeneralFlags);
  collectAdvanced(IOFrags);
  collectAdvanced(ProcessingFlags);
  collectAdvanced(FileOpFlags);

  // Configure formatter (unchanged)
  app.formatter_fn(makeHelpFormatter(
    general,
    io,
    processing,
    fileop,
    optRegistry.at("-h,--help"),
    advancedLongNames
  ));

  // ── applyMap: per-flag result population ──
  using ResultSetter = std::function<void(CmdParseResult&, CLI::Option const*)>;
  std::unordered_map<std::string_view, ResultSetter> applyMap;

  // General (app-level)
  applyMap["-h,--help"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.help = o->count() > 0;
  };
  applyMap["--version"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.version = o->count() > 0;
  };

  // General (group)
  applyMap["-v,--verbose"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.verbose = o->count() > 0;
  };
  applyMap["--log-json"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.jsonEnabled = o->count() > 0;
  };
  applyMap["-F,--full-progress"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.fullProgress = o->count() > 0;
  };
  applyMap["--color"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.color = o->as<std::string>();
  };
  applyMap["-y,--yes"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.yesToAll = o->count() > 0;
  };

  // IO
  applyMap["-i,--input"] = [](CmdParseResult& r, CLI::Option const* o) {
    if (o->count() > 0) { r.input = o->as<std::string>(); }
  };
  applyMap["-I,--inputs"] = [](CmdParseResult& r, CLI::Option const* o) {
    if (o->count() > 0) { r.inputs = o->as<std::vector<std::string>>(); }
  };
  applyMap["<positional>"] = [](CmdParseResult& r, CLI::Option const* o) {
    if (o->count() > 0) { r.positionalInputs = o->as<std::vector<std::string>>(); }
  };
  applyMap["-o,--output"] = [](CmdParseResult& r, CLI::Option const* o) {
    if (o->count() > 0) { r.output = o->as<std::string>(); }
  };
  applyMap["--state-file"] = [](CmdParseResult& r, CLI::Option const* o) {
    if (o->count() > 0) { r.stateFile = o->as<std::string>(); }
  };
  applyMap["-f,--output-format"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.outputFormat = o->as<std::string>();
  };
  applyMap["--keep"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.keep = o->count() > 0;
  };
  applyMap["--force-conflict-handling"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.forceConflictHandling = o->as<std::string>();
  };
  applyMap["-s,--folder-summary"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.folderSummary = o->count() > 0;
  };
  applyMap["-r,--recursive"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.recursive = o->count() > 0;
  };

  // Processing
  applyMap["-t,--type"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.processType = o->as<std::string>();
  };
  applyMap["-j,--jobs"] = [](CmdParseResult& r, CLI::Option const* o) {
    if (o->count() > 0) { r.maxJobs = o->as<std::size_t>(); }
  };
  applyMap["--resume"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.resume = o->count() > 0;
  };
  applyMap["--restart"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.restart = o->count() > 0;
  };
  applyMap["-x,--ffmpeg-path"] = [](CmdParseResult& r, CLI::Option const* o) {
    if (o->count() > 0) { r.ffmpegPath = o->as<std::string>(); }
  };
  applyMap["-c,--compress"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.compress = o->count() > 0;
  };
  applyMap["-q,--image-quality"] = [](CmdParseResult& r, CLI::Option const* o) {
    if (o->count() == 0) { return; }
    if (o->results().empty()) {
      r.error = "Option --image-quality requires a value.";
      return;
    }
    r.imageQuality = o->as<int>();
  };
  applyMap["--crf"] = [](CmdParseResult& r, CLI::Option const* o) {
    if (o->count() == 0) { return; }
    if (o->results().empty()) {
      r.error = "Option --crf requires a value.";
      return;
    }
    r.crf = o->as<int>();
  };
  applyMap["--min-vmaf"] = [](CmdParseResult& r, CLI::Option const* o) {
    if (o->count() == 0) { return; }
    if (o->results().empty()) {
      r.error = "Option --min-vmaf requires a value.";
      return;
    }
    r.minVmaf = o->as<int>();
  };
  applyMap["--dry-run"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.dryRun = o->count() > 0;
  };
  applyMap["--preset"] = [](CmdParseResult& r, CLI::Option const* o) {
    if (o->count() == 0) { return; }
    if (o->results().empty()) {
      r.error = "Option --preset requires a value.";
      return;
    }
    r.nvencPreset = o->as<std::string>();
  };
  applyMap["--video-codec"] = [](CmdParseResult& r, CLI::Option const* o) {
    if (o->count() == 0) { return; }
    if (o->results().empty()) {
      r.error = "Option --video-codec requires a value.";
      return;
    }
    r.videoCodec = o->as<std::string>();
  };

  // FileOp
  applyMap["-p,--pack"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.pack = o->count() > 0;
  };
  applyMap["-z,--pack-only"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.packOnly = o->count() > 0;
  };
  applyMap["-w,--overwrite"] = [](CmdParseResult& r, CLI::Option const* o) {
    r.overwrite = o->count() > 0;
  };

  // ── Parse and populate ──
  auto result = CmdParseResult{};
  try {
    app.parse(argc, argv);
    for (auto const& [name, setter]: applyMap) {
      auto const it = optRegistry.find(name);
      if (it != optRegistry.end()) { setter(result, it->second); }
    }
    result.helpText = app.help();
    if (app.got_subcommand(previewSub)) {
      result.preview = true;
      if (previewHelp->count() > 0) {
        result.help = true;
        result.helpText = buildPreviewHelpText();
      } else if (previewOriginal->count() == 0 || previewEncoded->count() == 0) {
        result.error = "preview requires two positional arguments: <original> <encoded>";
      } else {
        result.previewOriginal = previewOriginal->as<std::string>();
        result.previewEncoded = previewEncoded->as<std::string>();
        if (previewOutput->count() > 0) {
          result.previewOutput = previewOutput->as<std::string>();
        }
        if (previewStart->count() > 0) {
          result.previewStart = previewStart->as<double>();
        }
        if (previewDuration->count() > 0) {
          result.previewDuration = previewDuration->as<double>();
        }
        result.previewNoOpen = previewNoOpen->count() > 0;
      }
    }
  } catch (CLI::ParseError const& ex) {
    result.error = ex.what();
    result.helpText = app.help();
  }

  return result;
}
