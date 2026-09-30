#include "cmd/help_layout.h"

#include "infra/console_width.h"
#include "infra/env.h"
#include "infra/terminal.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::literals;

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

// Constant short-flag cell: every CLI11 short name is a single character, so
// "-x, " covers all rows; options without a short name render blanks so the
// long cells align in their own column.
constexpr auto kShortCellWidth = std::size_t{4};

auto formatShortCell(CLI::Option const* opt) -> std::string {
  auto const& snames = opt->get_snames();
  if (opt->get_positional() || snames.empty()) {
    return std::string(kShortCellWidth, ' ');
  }
  return "-" + std::string{snames.front()} + ", ";
}

auto formatLongCell(CLI::Option const* opt) -> std::string {
  if (opt->get_positional()) { return opt->get_name(true); }

  auto const hasLongName = [&lnames = opt->get_lnames()](std::string const& name) {
    return std::ranges::find(lnames, name) != lnames.end();
  };

  auto names = std::string{};
  auto const& lnames = opt->get_lnames();
  auto first = true;
  for (auto const& ln: lnames) {
    // collapse a registered negation pair (--pack, --no-pack) into --[no-]pack
    if (ln.starts_with("no-") && hasLongName(ln.substr(3))) { continue; }
    if (!first) names += ',';
    first = false;
    names += hasLongName("no-" + ln) ? "--[no-]" + ln : "--" + ln;
  }
  return names;
}

auto formatOptionName(CLI::Option const* opt) -> std::string {
  return formatShortCell(opt) + formatLongCell(opt);
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
  auto const styledDefaultText = defaultText.empty()
    ? std::string{}
    : terminal::styled(
        terminal::Stream::Stdout,
        terminal::roleStyle(terminal::Role::Accent) | fmt::emphasis::faint,
        defaultText
      );

  auto const coloredName = terminal::accent(nameStr);

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
    if (lineNum == 0) {
      result +=
        std::format("  {}{}{:<{}}{}\n", coloredName, styledDefaultText, "", gap, line);
    } else {
      result += std::format("{}{}\n", indent, line);
    }
  }

  if (result.ends_with('\n')) result.pop_back();
  return result;
}

auto formatGroupHeader(std::string const& name) -> std::string {
  if (name.empty()) return {};
  auto const coloredName =
    terminal::styled(terminal::Stream::Stdout, fmt::emphasis::bold, name);
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
  auto const coloredTitle =
    terminal::styled(terminal::Stream::Stdout, fmt::emphasis::bold, title);

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
  "debug"sv,
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

  auto result = std::format(
    "\n{}:\n",
    terminal::styled(terminal::Stream::Stdout, fmt::emphasis::bold, "encro commands")
  );
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
          terminal::accent(name),
          "",
          gap,
          wrappedDescription[lineNum]
        );
      } else {
        result += std::format("{}{}\n", indent, wrappedDescription[lineNum]);
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
    result += desc;
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
  std::span<CLI::App const* const> subcommands
) -> auto {
  return  //
    [general, io, processing, fileop, helpOpt, subcommands](
      CLI::App const* app_ptr,
      std::string /*prev*/
      // NOLINTNEXTLINE(performance-unnecessary-value-param): CLI11 formatter callback signature is fixed
      ,
      CLI::AppFormatMode /*mode*/
    ) -> std::string {
      // Subcommand synopsis lines stay out of the usage block: the commands
      // section below carries them with one-line descriptions.
      constexpr auto usageLines = std::array{
        "encro [<input>... | -i <input> | -I <file>...] [-o <output>] [-f mp4|webp] [-r] [-j <n>] [-p] [--resume|--restart]"sv,
        "encro -t picture <input> [-c [-q <n>]] [--video-webp] [-s] [-p]"sv,
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
        kAdvancedLongNames,
        fullTier
      );
      auto const colWidth = computeColumnWidth(maxColumnLen, layout);

      for (auto const* group: groupIter) {
        result += formatGroupHeader(group->get_description());
        for (auto const* opt: visibleOptionsOf(group, general, app_ptr)) {
          if (!hasOptionNames(opt)) continue;
          if (!fullTier && isAdvancedOption(opt, kAdvancedLongNames)) continue;
          result += formatOptionHelp(opt, colWidth, layout.lineLength);
          result += '\n';
        }
      }

      if (!fullTier) {
        result += '\n';
        result += terminal::styled(
          terminal::Stream::Stdout,
          terminal::roleStyle(terminal::Role::Muted),
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
      // NOLINTNEXTLINE(performance-unnecessary-value-param): CLI11 formatter callback signature is fixed
      ,
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

constexpr auto kPreviewUsageLines = std::array{
  "encro preview <original> [<encoded>] [--start <s>] [--duration <s>] [--output <path>] [--no-open]"sv,
};

constexpr auto kConfigUsageLines = std::array{
  "encro config <list|get <key>|set <key> <value>|unset <key>|path>"sv,
};

constexpr auto kCompletionUsageLines = std::array{
  "encro completion [--install | --uninstall] <powershell|bash>"sv,
  "encro completion powershell --install"sv,
};

constexpr auto kOrganizeUsageLines = std::array{
  "encro organize [dir] [-r] [--min-confidence <f>] [--model-dir <dir>] "
  "[--identity-tau <f>] [--download-models] [--dry-run]"sv,
};

}  // namespace

auto installHelpFormatter(
  CLI::App& app,
  CLI::App const* general,
  CLI::App const* io,
  CLI::App const* processing,
  CLI::App const* fileop,
  CLI::Option const* helpOpt,
  std::span<CLI::App const* const> subcommands
) -> void {
  app.formatter_fn(
    makeHelpFormatter(general, io, processing, fileop, helpOpt, subcommands)
  );
}

auto installOrganizeHelpFormatter(CLI::App& subApp) -> void {
  subApp.formatter_fn(makeSubcommandHelpFormatter(&subApp, kOrganizeUsageLines));
}

auto installPreviewHelpFormatter(CLI::App& subApp) -> void {
  subApp.formatter_fn(makeSubcommandHelpFormatter(&subApp, kPreviewUsageLines));
}

auto installConfigHelpFormatter(CLI::App& subApp) -> void {
  subApp.formatter_fn(makeSubcommandHelpFormatter(&subApp, kConfigUsageLines));
}

auto installCompletionHelpFormatter(CLI::App& subApp) -> void {
  subApp.formatter_fn(makeSubcommandHelpFormatter(&subApp, kCompletionUsageLines));
}
