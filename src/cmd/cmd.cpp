#include "cmd/cmd.h"

#include "infra/console_width.h"

#include <CLI/CLI.hpp>

#include <format>
#include <string>
#include <string_view>

using namespace std::literals;

namespace {

struct HelpTextLayout {
  unsigned lineLength;
  unsigned minDescriptionLength;
};

auto resolveHelpTextLayout() -> HelpTextLayout {
  auto const lineLength = static_cast<unsigned>(consolewidth::resolveColumns({
    .defaultColumns = 80,
    .minColumns = 40,
    .maxColumns = 120,
  }));

  return {
    .lineLength = lineLength,
    .minDescriptionLength = lineLength / 2,
  };
}

// ── formatter_fn helpers (no color — plain text, Phase 20 adds color) ──

auto formatOptionName(CLI::Option const* opt) -> std::string {
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

auto formatOptionHelp(CLI::Option const* opt, unsigned nameWidth) -> std::string {
  auto const nameStr = formatOptionName(opt);

  // Type placeholder
  auto typeStr = std::string{};
  if (opt->get_expected_min() > 0 && !opt->get_type_name().empty()) {
    auto const typeName = opt->get_type_name();
    if (typeName != "TEXT"sv && typeName != "text"sv) { typeStr = " " + typeName; }
  }

  // Default value display
  auto defaultStr = std::string{};
  if (!opt->get_default_str().empty()) {
    defaultStr = " (=" + opt->get_default_str() + ")";
  }

  auto const description = opt->get_description();

  // Calculate padding
  auto const paddedName = nameStr + typeStr + defaultStr;
  auto const padSize = paddedName.size() < nameWidth ? nameWidth - paddedName.size() : 2u;

  auto result = std::format("  {}{}{:<{}}{}", paddedName, "", "", padSize, description);
  return result;
}

auto formatGroupHeader(std::string const& name) -> std::string {
  if (name.empty()) return {};
  return std::format("\n{}:\n", name);
}

auto makeHelpFormatter(unsigned lineLength, unsigned minDescriptionLength) -> auto {
  return [lineLength, minDescriptionLength](
           CLI::App const* app_ptr,
           std::string /*prev*/,
           CLI::AppFormatMode /*mode*/
         ) -> std::string {
    auto const& app = *app_ptr;
    auto result = std::string{};
    result += formatGroupHeader(app.get_description());

    // Determine name width for adaptive column sizing
    auto maxNameLen = 0u;
    for (auto const* opt: app.get_options()) {
      if (!opt->get_lnames().empty() || !opt->get_snames().empty()) {
        auto const nameStr = formatOptionName(opt);
        auto typeStr = std::string{};
        if (opt->get_expected_min() > 0 && !opt->get_type_name().empty()) {
          auto const typeName = opt->get_type_name();
          if (typeName != "TEXT"sv && typeName != "text"sv) { typeStr = " " + typeName; }
        }
        auto defaultStr = std::string{};
        if (!opt->get_default_str().empty()) {
          defaultStr = " (=" + opt->get_default_str() + ")";
        }
        auto const fullLen = nameStr.size() + typeStr.size() + defaultStr.size() + 2;
        maxNameLen = std::max(maxNameLen, static_cast<unsigned>(fullLen));
      }
    }

    // Clamp name width
    auto const nameWidth = std::max(
      minDescriptionLength,
      std::min(maxNameLen, lineLength - minDescriptionLength)
    );

    // Walk subgroups (option groups) in defined order
    auto const& subgroups = app.get_subcommands();
    for (auto const* sub: subgroups) {
      result += formatGroupHeader(sub->get_description());
      for (auto const* opt: sub->get_options()) {
        if (opt->get_lnames().empty() && opt->get_snames().empty()) continue;
        result += formatOptionHelp(opt, nameWidth);
        result += '\n';
      }
    }

    // Also print any options directly on the app (not in a group)
    auto directOpts =
      app.get_options([](CLI::Option const* opt) { return opt->get_group().empty(); });
    if (!directOpts.empty()) {
      for (auto const* opt: directOpts) {
        if (opt->get_lnames().empty() && opt->get_snames().empty()) continue;
        // Skip help/version flags handled by CLI11
        if (opt->get_lnames().size() == 1 && opt->get_lnames()[0] == "help") continue;
        result += formatOptionHelp(opt, nameWidth);
        result += '\n';
      }
    }

    return result;
  };
}

}  // namespace

auto commandLineInit(int argc, char* argv[]) -> CmdParseResult {
  auto const layout = resolveHelpTextLayout();

  auto app = CLI::App{"Allowed options"};

  // Disable automatic help flag so we can handle --help manually
  // (CLI11's built-in help would throw CLI::Success before result.help is set)
  app.set_help_flag("");

  // ── General options ────────────────────────────────────────
  auto* general = app.add_option_group("General", "General options");

  auto* helpFlag = app.add_flag("-h,--help", "produce help message");

  auto* verboseFlag = general->add_flag("-v,--verbose", "enable verbose output");
  auto* verboseEchoFlag = general->add_flag(
    "-e,--verbose-echo",
    "echo verbose logs to console (disable progress bars)"
  );
  auto* fullProgressFlag = general->add_flag(
    "-F,--full-progress",
    "show full progress with per-worker encoding bars and per-archive packing bars"
  );
  auto* colorOpt = general->add_option("--color", "terminal colors: auto, always, never")
                     ->default_str("auto");
  auto* yesFlag = general->add_flag("-y,--yes", "automatic yes to prompts");

  // ── Input/Output options ───────────────────────────────────
  auto* io = app.add_option_group("IO", "Input/Output options");

  auto* inputOpt = io->add_option("-i,--input", "input file or directory path");
  auto* inputsOpt = io->add_option("-I,--inputs", "input video file paths");
  auto* outputOpt = io->add_option(
    "-o,--output",
    "custom output directory path\n  aliases: + or input:// for input root, = or "
    "common:// for common root"
  );
  auto* stateFileOpt = io->add_option("--state-file", "custom job state file path");
  auto* outputFormatOpt =
    io->add_option("-f,--output-format", "target format: mp4 or webp")
      ->default_str("mp4");
  auto* flatFlag =
    io->add_flag("--flat", "flatten output names inside the output directory (default)");
  auto* keepFlag = io->add_flag(
    "--keep",
    "preserve relative input subdirectories inside the output directory"
  );
  auto* forceConflictOpt =
    io->add_option(
        "--force-conflict-handling",
        "control collision-safe file names for unique flat outputs: y or n"
    )
      ->default_str("y");
  auto* folderSummaryFlag = io->add_flag(
    "-s,--folder-summary",
    "enable picture-mode folder summary images in flat packs"
  );
  auto* recursiveFlag = io->add_flag("-r,--recursive", "enable recursively search");

  // ── Processing options ─────────────────────────────────────
  auto* processing = app.add_option_group("Processing", "Processing options");

  auto* typeOpt =
    processing->add_option("-t,--type", "process type: video(vid)|picture(pic)")
      ->default_str("video");
  auto* jobsOpt =
    processing->add_option("-j,--jobs", "max parallel jobs (>=1, default=10)")
      ->default_str("10");
  auto* resumeFlag =
    processing
      ->add_flag("--resume", "resume previous unfinished job state when available");
  auto* restartFlag =
    processing->add_flag("--restart", "ignore previous job state and start a fresh run");
  auto* ffmpegPathOpt =
    processing->add_option("-x,--ffmpeg-path", "custom ffmpeg install path");
  auto* compressFlag =
    processing
      ->add_flag("-c,--compress", "enable JPEG compression during picture processing");
  auto* imageQualityOpt = processing->add_option(
    "-q,--image-quality",
    "JPEG compression quality (2-31, default=5, lower=better)"
  );

  // ── File operation options ─────────────────────────────────
  auto* fileop = app.add_option_group("FileOp", "File operation options");

  auto* packFlag =
    fileop->add_flag("-p,--pack", "pack encoded video outputs into zip files");
  auto* packOnlyFlag =
    fileop->add_flag("-z,--pack-only", "pack only: zip all files in input directory");
  auto* overwriteFlag =
    fileop->add_flag("-w,--overwrite", "overwrite existing files without prompt");

  // ── Configure formatter ────────────────────────────────────
  app.formatter_fn(makeHelpFormatter(layout.lineLength, layout.minDescriptionLength));

  // ── Parse ──────────────────────────────────────────────────
  auto result = CmdParseResult{};
  try {
    app.parse(argc, argv);

    // ── Populate result struct ───────────────────────────────
    // General
    result.help = helpFlag->count() > 0;
    result.verbose = verboseFlag->count() > 0;
    result.verboseEcho = verboseEchoFlag->count() > 0;
    result.fullProgress = fullProgressFlag->count() > 0;
    result.color = colorOpt->as<std::string>();
    result.yesToAll = yesFlag->count() > 0;

    // Input/Output
    if (inputOpt->count() > 0) { result.input = inputOpt->as<std::string>(); }
    if (inputsOpt->count() > 0) {
      result.inputs = inputsOpt->as<std::vector<std::string>>();
    }
    if (outputOpt->count() > 0) { result.output = outputOpt->as<std::string>(); }
    if (stateFileOpt->count() > 0) { result.stateFile = stateFileOpt->as<std::string>(); }
    result.outputFormat = outputFormatOpt->as<std::string>();
    result.flat = flatFlag->count() > 0;
    result.keep = keepFlag->count() > 0;
    result.forceConflictHandling = forceConflictOpt->as<std::string>();
    result.folderSummary = folderSummaryFlag->count() > 0;
    result.recursive = recursiveFlag->count() > 0;

    // Processing
    result.processType = typeOpt->as<std::string>();
    if (jobsOpt->count() > 0) { result.maxJobs = jobsOpt->as<std::size_t>(); }
    result.resume = resumeFlag->count() > 0;
    result.restart = restartFlag->count() > 0;
    if (ffmpegPathOpt->count() > 0) {
      result.ffmpegPath = ffmpegPathOpt->as<std::string>();
    }
    result.compress = compressFlag->count() > 0;
    if (imageQualityOpt->count() > 0) {
      result.imageQuality = imageQualityOpt->as<int>();
    }

    // File operation
    result.pack = packFlag->count() > 0;
    result.packOnly = packOnlyFlag->count() > 0;
    result.overwrite = overwriteFlag->count() > 0;

    // Help text (always rendered, displayed only when --help is set)
    result.helpText = app.help();
  } catch (CLI::ParseError const& ex) {
    result.error = ex.what();
    // Still populate helpText for error display context
    result.helpText = app.help();
  }

  return result;
}
