#include "cmd/cmd.h"

#include "cmd/completion_registry.h"
#include "cmd/config_store.h"
#include "cmd/help_layout.h"
#include "cmd/option_specs.h"

#include <CLI/CLI.hpp>

#include <array>
#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace {

// Subcommand names take precedence over positional input interpretation;
// bare invocations fall through to the encode workflow unchanged.

// Local character grouping (spec: image-character-organize). Analysis is
// fully local: image content never leaves this machine.
auto registerOrganizeSubcommand(CLI::App& app, CmdParseResult& result) -> CLI::App* {
  auto* sub = app.add_subcommand(
    "organize",
    "group a folder of images into per-character folders with a local "
    "model; image content never leaves this machine"
  );
  sub->set_help_flag("-h,--help", "show organize help");
  auto const options = std::tuple{
    opt("dir", &result.organizeDir, "folder of images to organize", cfg::Required{}),
    opt(
      "-r,--recursive,--no-recursive{false}",
      &result.recursive,
      "also scan subdirectories"
    ),
    opt(
      "--min-confidence",
      &result.organizeMinConfidence,
      "confidence floor for appearance and subject-count tags (0-1)",
      cfg::RequiredDefault{"0.35"},
      cfg::FloatRange{0.0, 1.0}
    ),
    opt(
      "--model-dir",
      &result.organizeModelDir,
      "directory holding the local model files (default: ~/.encro/models)",
      cfg::ConfigKey{"model-dir"},
      cfg::Path{}
    ),
    opt(
      "--download-models",
      &result.organizeDownloadModels,
      "fetch missing model files (~530 MB, one time), then run"
    ),
    opt("--dry-run", &result.dryRun, "classify and print the plan; copy nothing"),
    opt("--recluster", &result.organizeRecluster, "discard cached analysis and redo it"),
  };
  registerAll(sub, options, result.keyEntries);
  installOrganizeHelpFormatter(*sub);
  return sub;
}

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
      "output video path (default: <original-dir>/<original-stem>.preview.mp4)",
      cfg::Path{}
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
  registerAll(sub, options, result.keyEntries);
  installPreviewHelpFormatter(*sub);
  return sub;
}

// Persistent user-level configuration (spec: user-config). Git-style
// positional actions (`config set <key> <value>`); bare `encro config` shows
// the subcommand help.
auto registerConfigSubcommand(CLI::App& app, CmdParseResult& result) -> CLI::App* {
  auto* sub =
    app.add_subcommand("config", "inspect and persist user-level configuration defaults");
  sub->set_help_flag("-h,--help", "show config help");
  auto const options = std::tuple{
    opt(
      "action",
      &result.configVerb,
      "list | get <key> | set <key> <value> | unset <key> | path",
      cfg::Members{"list", "get", "set", "unset", "path"}
    ),
    opt("key", &result.configKey, "config key (get/set/unset)"),
    opt("value", &result.configValue, "value to persist (set)"),
  };
  registerAll(sub, options, result.keyEntries);
  installConfigHelpFormatter(*sub);
  return sub;
}

// The declarative table cannot express "each action fixes how many of the
// key/value positionals are present"; words past the last positional are
// rejected natively by CLI11, this closes the under-filled cases through the
// same native-error channel (spec: wrong argument count exits non-zero).
auto configActionArityError(CmdParseResult const& result) -> std::optional<std::string> {
  auto const required = result.configVerb == "set"               ? 2
    : result.configVerb == "get" || result.configVerb == "unset" ? 1
                                                                 : 0;
  auto const given = static_cast<int>(result.configKey.has_value())
    + static_cast<int>(result.configValue.has_value());
  if (given != required) {
    return std::format(
      "config {}: {} argument{} expected, but {} given",
      result.configVerb,
      required,
      required == 1 ? "" : "s",
      given
    );
  }
  return std::nullopt;
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
  registerAll(sub, options, result.keyEntries);
  installCompletionHelpFormatter(*sub);
  return sub;
}

auto registerGeneralFlags(CLI::App& app, CLI::App* general, CmdParseResult& result)
  -> CLI::Option* {
  // help/version bypass the spec table: the -hh two-tier mechanism needs the
  // help option pointer returned to the caller
  auto* helpOpt =
    app.add_flag("-h,--help", result.help, "show help; use -hh to show all options");
  app.add_flag("--version", result.version, "show version information");
  // Occurrence counting for -vv is a CLI11 flag trait, not an option; kept
  // beside help/version because it bypasses the spec table.
  app.add_flag(
    "-v,--verbose",
    result.verbosity,
    "echo progress detail to the terminal (stderr); repeat for full debug "
    "(disables progress bars)"
  );
  auto const options = std::tuple{
    opt(
      "--quiet",
      &result.quiet,
      "suppress narration and progress output; errors and the summary still print"
    ),
    opt(
      "--log-json",
      &result.jsonEnabled,
      "enable NDJSON structured log output (one JSON object per line)"
    ),
    opt("--debug", &result.debug, "echo full debug diagnostics to the terminal (stderr)"),
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
  registerAll(general, options, result.keyEntries);

  // --color must commit before any help flag short-circuits the parse:
  // subcommand -h flags run at First priority and throw CallForHelp before
  // Normal callbacks, so without this the CLI/config color value never
  // reaches the binding on help paths and prelude would configure auto.
  app.get_option("--color")->callback_priority(CLI::CallbackPriority::FirstPreHelp);
  return helpOpt;
}

void registerIoFlags(CLI::App* io, CmdParseResult& result) {
  constexpr auto kMaxPositionalInputs = 1000000;
  auto const options = std::tuple{
    opt(
      "-i,--input",
      &result.input,
      "input file or directory path",
      cfg::Excludes{"--inputs"},
      cfg::Path{}
    ),
    opt(
      "-I,--inputs",
      &result.inputs,
      "input video file paths",
      cfg::Expected{0, kMaxPositionalInputs},
      cfg::Path{}
    ),
    opt(
      "-o,--output",
      &result.output,
      "custom output directory path\n  aliases: + or input:// for input "
      "root, = or common:// for common root",
      cfg::Path{}
    ),
    opt("--state-file", &result.stateFile, "custom job state file path", cfg::Path{}),
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
  registerAll(io, options, result.keyEntries);
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
  registerAll(io, positional, result.keyEntries);
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
      cfg::ConfigKey{"ffmpeg-path"},
      cfg::Path{}
    ),
    opt(
      "-c,--compress,--no-compress{false}",
      &result.compress,
      "enable JPEG compression during picture processing",
      cfg::ConfigKey{"compress"}
    ),
    opt(
      "--video-webp",
      &result.videoWebp,
      "convert videos found in a picture input to animated WebP"
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
  registerAll(processing, options, result.keyEntries);
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
  registerAll(fileop, options, result.keyEntries);
}

// Loads the user config and applies each stored value as a forced option
// default (design D1). The table supplies the known-key set. Returns a
// load-error message, or nullopt.
auto injectConfigDefaults(CLI::App& app, configstore::KeyTable const& table)
  -> std::optional<std::string> {
  auto const configPath = configstore::resolveConfigPath();
  auto const loaded = configstore::load(configPath, table);
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

  // Sub-only config keys (model-dir lives on the organize subcommand, not
  // the parent app): inject as forced defaults on the sub's own option.
  // Safe against explicit CLI values: the callback reads parsed results and
  // only fires when that subcommand is parsed.
  for (auto const& [key, value]: loaded.values) {
    if (app.get_option_no_throw("--" + key) != nullptr) { continue; }
    for (auto* subc: app.get_subcommands([](CLI::App*) { return true; })) {
      if (subc->get_name().empty()) { continue; }
      if (auto* subOpt = subc->get_option_no_throw("--" + key)) {
        subOpt->default_str(value);
        subOpt->force_callback();
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
  // Default help target so every return path (including the config-injection
  // error below) yields renderable help; subcommand matches overwrite it.
  result.helpApp_ = tree.app;
  if (result.error.has_value()) { return result; }

  // result is the SAME object the options were bound to at registration time
  // (bound callbacks write into it during parse).
  try {
    tree.app->parse(argc, argv);
    result.helpApp_ = tree.app;
    if (result.debug) { result.verbosity = std::max(result.verbosity, 2); }
    result.verbosity = std::min(result.verbosity, 2);
    if (tree.app->got_subcommand(tree.previewSub)) { result.preview = true; }
    if (tree.app->got_subcommand(tree.organizeSub)) { result.organize = true; }
    if (tree.app->got_subcommand(tree.configSub)) {
      result.config = true;
      result.helpApp_ = tree.configSub;
      if (auto const error = configActionArityError(result); error.has_value()) {
        result.error = *error;
      }
    }
    if (tree.app->got_subcommand(tree.completionSub)) {
      result.completion = true;
      result.helpApp_ = tree.completionSub;
    }
  } catch (CLI::CallForHelp const&) {
    // The preview/config/completion subcommands carry the native help flags
    // (the parent app cleared its own), so the help text comes from whichever
    // matched.
    result.help = true;
    if (tree.app->got_subcommand(tree.completionSub)) {
      result.helpApp_ = tree.completionSub;
    } else if (tree.app->got_subcommand(tree.configSub)) {
      result.helpApp_ = tree.configSub;
    } else if (tree.app->got_subcommand(tree.organizeSub)) {
      result.helpApp_ = tree.organizeSub;
    } else {
      result.helpApp_ = tree.previewSub;
    }
  } catch (CLI::ParseError const& ex) {
    result.error = ex.what();
    result.helpApp_ = tree.app;
  }
  return result;
}

}  // namespace

auto buildAppTree(CmdParseResult& result, std::string const& introLine, bool injectConfig)
  -> AppTree {
  // Leaked on purpose (never freed): the completion registry keeps pointers to
  // positional options (design D2), so the app must outlive this call. One
  // small allocation per parse keeps them process-lifetime.
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
  auto* organizeSub = registerOrganizeSubcommand(*app, result);
  auto* configSub = registerConfigSubcommand(*app, result);
  auto* completionSub = registerCompletionSubcommand(*app, result);
  // Option groups are CLI11 subcommands too, so the commands section is given
  // the real subcommand apps explicitly. The array is leaked with the app:
  // the formatter lambda captures the span by value, while parse and help
  // rendering happen after this call returns.
  auto* const commandSubs =
    new std::array<CLI::App const*, 4>{previewSub, organizeSub, configSub, completionSub};

  // The leaked commandSubs array outlives the formatter's span.
  installHelpFormatter(
    *app,
    general,
    io,
    processing,
    fileop,
    helpOpt,
    std::span{*commandSubs}
  );

  // ── Config-key table (design D1/D3): assembled once after the last
  // registration, so a token that drifts from the canonical order fails the
  // run here instead of silently changing `config list`.
  auto const table =
    configstore::assembleKeyTable(configstore::canonicalKeyOrder(), result.keyEntries);
  if (table.has_value()) {
    result.keyTable = *table;
    // Completion reads the same table (design D6): its key -> long-name map is
    // a projection of the assembled key set.
    for (auto const& def: result.keyTable.keys) {
      completion::recordConfigKey(def.key, def.longName);
    }
  } else {
    result.error = table.error();
  }

  // ── Config injection (design D1): config values become forced option
  // defaults, so CLI values win, validators run on applied defaults, and the
  // help (=default) display shows effective defaults. A failed table already
  // failed the run, so it skips injection.
  if (injectConfig && !result.error.has_value()) {
    if (
      auto const error = injectConfigDefaults(*app, result.keyTable); error.has_value()
    ) {
      result.error = *error;
    }
  }
  return AppTree{app, previewSub, organizeSub, configSub, completionSub};
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
