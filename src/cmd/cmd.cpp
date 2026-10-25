#include "cmd/cmd.h"

#include "cmd/completion_registry.h"
#include "cmd/config_builder.h"
#include "cmd/config_store.h"
#include "cmd/help_layout.h"
#include "cmd/option_specs.h"
#include "cmd/template_store.h"
#include "core/path_utf8.h"
#include "infra/terminal.h"
#include "organize/disposition.h"
#include "organize/organize_types.h"
#include "tagger/engine_factory.h"

#include <CLI/CLI.hpp>

#include <array>
#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

// Subcommand names take precedence over positional input interpretation;
// bare invocations fall through to the encode workflow unchanged.

// Local character grouping (spec: image-character-organize). Analysis is
// fully local: image content never leaves this machine.
auto registerOrganizeSubcommand(CLI::App& app, CmdParseResult& result) -> CLI::App* {
  // Folder flags are greedy per occurrence like --inputs; one name per
  // occurrence is the documented shape.
  constexpr auto kMaxFolderFlags = 256;
  auto* sub = app.add_subcommand(
    "organize",
    "group a folder of images into per-character folders with a local "
    "model; image content never leaves this machine"
  );
  sub->set_help_flag("-h,--help", "show organize help");
  auto const options = std::tuple{
    opt("dir", &result.organizeDir, "folder of images to organize"),
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
      "--identity-tau",
      &result.organizeIdentityTau,
      std::format(
        "identity similarity threshold: raise it to keep look-alikes apart, lower "
        "it to hold a character's varied artwork together (default {:.2f})",
        organize::kCombinedTau
      ),
      cfg::ConfigKey{"identity-tau"},
      cfg::DefaultValue{std::format("{:.2f}", organize::kCombinedTau)},
      cfg::FloatRange{0.0, 1.0},
      cfg::PositiveFloat{}
    ),
    opt(
      "--download-models",
      &result.organizeDownloadModels,
      "fetch missing model files (~530 MB), then run (no dir: fetch only)"
    ),
    opt(
      "--dry-run,--no-dry-run{false}",
      &result.dryRun,
      "classify and print the plan; copy nothing"
    ),
    opt("--recluster", &result.organizeRecluster, "discard cached analysis and redo it"),
    opt(
      "--ingest",
      &result.organizeIngest,
      "treat this first-level folder's images as input (repeatable)",
      cfg::Expected{1, kMaxFolderFlags}
    ),
    opt(
      "--ignore-folder",
      &result.organizeIgnoreFolder,
      "skip this first-level folder entirely (repeatable)",
      cfg::Expected{1, kMaxFolderFlags}
    ),
  };
  registerAll(sub, options, result.keyEntries);
  installOrganizeHelpFormatter(*sub);
  // Hidden, not unregistered: an empty CLI11 group is its "not in help"
  // convention, and the help/completion renderers skip it (design D2).
  if (!tagger::inferenceAvailable()) { sub->group(""); }
  return sub;
}

auto registerBatchSubcommand(CLI::App& app, CmdParseResult& result) -> CLI::App* {
  auto* sub = app.add_subcommand(
    "batch",
    "process every first-level subfolder of a root directory as its own task"
  );
  sub->set_help_flag("-h,--help", "show batch help");
  // The processing options mirror the main workflow's bindings (no config-key
  // tokens: the keys are owned by the main options, and the injection step
  // syncs these twins' displayed defaults).
  auto const options = std::tuple{
    opt(
      "-o,--output",
      &result.batchOutput,
      "packed output root (default: <root>/packed); plain directory path only",
      cfg::Path{}
    ),
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
      "--resume,--no-resume{false}",
      &result.resume,
      "require matching previous job state; error if missing or mismatched",
      cfg::Excludes{"--restart"}
    ),
    opt(
      "--restart,--no-restart{false}",
      &result.restart,
      "ignore previous job state and start a fresh run"
    ),
    opt(
      "-x,--ffmpeg-path",
      &result.ffmpegPath,
      "custom ffmpeg install path",
      cfg::Path{}
    ),
    opt(
      "-c,--compress,--no-compress{false}",
      &result.compress,
      "enable JPEG compression during picture processing"
    ),
    opt(
      "--video-webp,--no-video-webp{false}",
      &result.videoWebp,
      "convert videos found in a picture input to animated WebP"
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
      cfg::OptionalDefault{std::to_string(appctx::kDefaultMinVmaf)},
      cfg::Range{0, 100}
    ),
    opt(
      "--dry-run,--no-dry-run{false}",
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
    opt(
      "-p,--pack,--no-pack{false}",
      &result.pack,
      "pack encoded video outputs into zip files",
      cfg::Excludes{"--pack-only"}
    ),
    opt(
      "-z,--pack-only,--no-pack-only{false}",
      &result.packOnly,
      "pack only: zip all files in input directory"
    ),
    opt(
      "-f,--output-format",
      &result.outputFormat,
      "target format: mp4 or webp",
      cfg::OptionalDefault{"mp4"},
      cfg::Members{"mp4", "webp"}
    ),
    opt(
      "--keep,--no-keep{false}",
      &result.keep,
      "preserve relative input subdirectories inside the output directory "
      "(default: flatten)"
    ),
    opt(
      "--force-conflict-handling",
      &result.forceConflictHandling,
      "same-name collisions in flat output: y=auto-rename, n=allow duplicates",
      cfg::OptionalDefault{"y"},
      cfg::CheckedTransformer{{"y", "y"}, {"Y", "y"}, {"n", "n"}, {"N", "n"}}
    ),
    opt(
      "-s,--folder-summary,--no-folder-summary{false}",
      &result.folderSummary,
      "enable picture-mode folder summary images in flat packs"
    ),
    opt(
      "-r,--recursive,--no-recursive{false}",
      &result.recursive,
      "enable recursively search"
    ),
    opt("-y,--yes,--no-yes{false}", &result.yesToAll, "automatic yes to prompts"),
  };
  // The positional root and the exclusion stubs are registered by hand: the
  // exclusion pair is wired by pointer (cfg::Excludes resolves long names, and
  // the root positional has none).
  auto* rootOpt = sub->add_option(
    "root",
    result.batchRoot,
    "root directory whose first-level subfolders are the tasks"
  );
  cfg::Required{}(rootOpt);
  registerAll(sub, options, result.keyEntries);
  auto* inputsStub = sub->add_option(
    "-I,--inputs",
    result.batchInputsStub,
    "not supported with batch: the positional root replaces input lists"
  );
  cfg::Expected{0, 1000000}(inputsStub);
  cfg::Path{}(inputsStub);
  inputsStub->excludes(rootOpt);
  auto* stateFileStub = sub->add_option(
    "--state-file",
    result.stateFile,
    "not supported with batch: each subfolder keeps its own state file"
  );
  cfg::Path{}(stateFileStub);
  installBatchHelpFormatter(*sub);
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
      cfg::OptionalDefault{std::to_string(appctx::kDefaultMinVmaf)},
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
auto actionArityError(
  std::string_view subcommand,
  std::string const& verb,
  int required,
  int given
) -> std::optional<std::string> {
  if (given == required) { return std::nullopt; }
  return std::format(
    "{} {}: {} argument{} expected, but {} given",
    subcommand,
    verb,
    required,
    required == 1 ? "" : "s",
    given
  );
}

auto configActionArityError(CmdParseResult const& result) -> std::optional<std::string> {
  auto const required = result.configVerb == "set"               ? 2
    : result.configVerb == "get" || result.configVerb == "unset" ? 1
                                                                 : 0;
  auto const given = static_cast<int>(result.configKey.has_value())
    + static_cast<int>(result.configValue.has_value());
  return actionArityError("config", result.configVerb, required, given);
}

// --download-models is a complete request on its own: fetching the models
// needs no images. Beside the config arity rule so the missing-directory case
// keeps the CLI's own error line and help hint (design D1).
auto organizeDirError(CmdParseResult const& result) -> std::optional<std::string> {
  if (result.organizeDir.has_value() || result.organizeDownloadModels) {
    return std::nullopt;
  }
  return "dir is required";
}

// Command templates (spec: command-templates). Git-style positional actions
// like `config`, plus CLI11 fallthrough so `encro template save anime --crf 24`
// resolves the parent's options after the subcommand name. Scoped to this
// subcommand: a global fallthrough would weaken unknown-option errors
// everywhere else.
auto registerTemplateSubcommand(CLI::App& app, CmdParseResult& result) -> CLI::App* {
  auto* sub = app.add_subcommand(
    "template",
    "store the argument cluster that shapes one kind of task and replay it "
    "with -T"
  );
  sub->set_help_flag("-h,--help", "show template help");
  sub->fallthrough();
  auto const options = std::tuple{
    opt(
      "action",
      &result.templateVerb,
      "list | show <name> | save <name> [options] | unset <name> <key> | "
      "delete <name> | path | schema",
      cfg::Members{"list", "show", "save", "unset", "delete", "path", "schema"}
    ),
    opt(
      "name",
      &result.templateName,
      "template name (show/save/unset/delete)",
      cfg::Provider{completion::kTemplateNameProvider}
    ),
    opt("key", &result.templateKey, "template key (unset)"),
  };
  registerAll(sub, options, result.keyEntries);
  installTemplateHelpFormatter(*sub);
  return sub;
}

// Sibling of configActionArityError: each action fixes how many of the name/key
// positionals are present; words past the last positional are rejected natively.
auto templateActionArityError(CmdParseResult const& result)
  -> std::optional<std::string> {
  auto const required = result.templateVerb == "unset" ? 2
    : result.templateVerb == "show"
      || result.templateVerb == "save"
      || result.templateVerb == "delete"
    ? 1
    : 0;
  auto const given = static_cast<int>(result.templateName.has_value())
    + static_cast<int>(result.templateKey.has_value());
  return actionArityError("template", result.templateVerb, required, given);
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
    opt(
      "--candidates",
      &result.completionCandidates,
      "print one dynamic slot's candidates, one per line (called by the "
      "generated scripts)",
      cfg::Excludes{"--install"},
      cfg::Excludes{"--uninstall"}
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
      "-T,--template",
      &result.selectedTemplate,
      "replay a stored argument template by name (without a name: list "
      "templates)",
      cfg::Expected{0, 1},
      // Names live on disk, so the emitted script asks the binary for them at
      // completion time (complete-template-names design D2).
      cfg::Provider{completion::kTemplateNameProvider}
    ),
    opt(
      "--quiet,--no-quiet{false}",
      &result.quiet,
      "suppress narration and progress output; errors and the summary still print",
      cfg::TemplateKey{"quiet"}
    ),
    opt(
      "--log-json,--no-log-json{false}",
      &result.jsonEnabled,
      "enable NDJSON structured log output (one JSON object per line)",
      cfg::TemplateKey{"log-json"}
    ),
    opt(
      "--debug,--no-debug{false}",
      &result.debug,
      "echo full debug diagnostics to the terminal (stderr)",
      cfg::TemplateKey{"debug"}
    ),
    opt(
      "-F,--full-progress,--no-full-progress{false}",
      &result.fullProgress,
      "show full progress with per-worker encoding bars and per-archive packing "
      "bars",
      cfg::TemplateKey{"full-progress"}
    ),
    opt(
      "--color",
      &result.color,
      "terminal colors: auto, always, never",
      cfg::OptionalDefault{"auto"},
      cfg::ConfigKey{"color"},
      cfg::TemplateKey{"color"},
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
      cfg::ConfigKey{"yes"},
      cfg::TemplateKey{"yes"}
    ),
  };
  registerAll(general, options, result.keyEntries, &result.templateEntries);

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
      cfg::TemplateKey{"output"},
      cfg::Path{}
    ),
    opt("--state-file", &result.stateFile, "custom job state file path", cfg::Path{}),
    opt(
      "-f,--output-format",
      &result.outputFormat,
      "target format: mp4 or webp",
      cfg::OptionalDefault{"mp4"},
      cfg::ConfigKey{"output-format"},
      cfg::TemplateKey{"output-format"},
      cfg::Members{"mp4", "webp"}
    ),
    opt(
      "--keep,--no-keep{false}",
      &result.keep,
      "preserve relative input subdirectories inside the output directory "
      "(default: flatten)",
      cfg::ConfigKey{"keep"},
      cfg::TemplateKey{"keep"}
    ),
    opt(
      "--force-conflict-handling",
      &result.forceConflictHandling,
      "same-name collisions in flat output: y=auto-rename, n=allow "
      "duplicates",
      cfg::OptionalDefault{"y"},
      cfg::ConfigKey{"force-conflict-handling"},
      cfg::TemplateKey{"force-conflict-handling"},
      cfg::CheckedTransformer{{{"y", "y"}, {"Y", "y"}, {"n", "n"}, {"N", "n"}}}
    ),
    opt(
      "-s,--folder-summary,--no-folder-summary{false}",
      &result.folderSummary,
      "enable picture-mode folder summary images in flat packs",
      cfg::ConfigKey{"folder-summary"},
      cfg::TemplateKey{"folder-summary"}
    ),
    opt(
      "-r,--recursive,--no-recursive{false}",
      &result.recursive,
      "enable recursively search",
      cfg::ConfigKey{"recursive"},
      cfg::TemplateKey{"recursive"}
    ),
  };
  registerAll(io, options, result.keyEntries, &result.templateEntries);
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

// NOLINTNEXTLINE(readability-function-size): declarative option table
void registerProcessingFlags(CLI::App* processing, CmdParseResult& result) {
  auto const options = std::tuple{
    opt(
      "-t,--type",
      &result.processType,
      "process type: video(vid)|picture(pic)",
      cfg::OptionalDefault{"video"},
      cfg::TemplateKey{"type"},
      cfg::CheckedTransformer{{"vid", "video"}, {"pic", "picture"}}
    ),
    opt(
      "-j,--jobs",
      &result.maxJobs,
      "max parallel jobs (>=1)",
      cfg::OptionalDefault{"10"},
      cfg::ConfigKey{"jobs"},
      cfg::TemplateKey{"jobs"},
      cfg::PositiveNumber{}
    ),
    opt(
      "--resume,--no-resume{false}",
      &result.resume,
      "require matching previous job state; error if missing or mismatched",
      cfg::TemplateKey{"resume"},
      cfg::Excludes{"--restart"}
    ),
    opt(
      "--restart,--no-restart{false}",
      &result.restart,
      "ignore previous job state and start a fresh run",
      cfg::TemplateKey{"restart"}
    ),
    opt(
      "-x,--ffmpeg-path",
      &result.ffmpegPath,
      "custom ffmpeg install path",
      cfg::ConfigKey{"ffmpeg-path"},
      cfg::TemplateKey{"ffmpeg-path"},
      cfg::Path{}
    ),
    opt(
      "-c,--compress,--no-compress{false}",
      &result.compress,
      "enable JPEG compression during picture processing",
      cfg::ConfigKey{"compress"},
      cfg::TemplateKey{"compress"}
    ),
    opt(
      "--video-webp,--no-video-webp{false}",
      &result.videoWebp,
      "convert videos found in a picture input to animated WebP",
      cfg::TemplateKey{"video-webp"}
    ),
    opt(
      "-q,--image-quality",
      &result.imageQuality,
      "JPEG compression quality (2-31, lower=better)",
      cfg::RequiredDefault{"2"},
      cfg::ConfigKey{"image-quality"},
      cfg::TemplateKey{"image-quality"},
      cfg::Range{2, 31},
      cfg::Needs{"--compress"}
    ),
    opt(
      "--crf",
      &result.crf,
      "video encode quality (0-51, lower=better)",
      cfg::RequiredDefault{"28"},
      cfg::ConfigKey{"crf"},
      cfg::TemplateKey{"crf"},
      cfg::Range{0, 51}
    ),
    opt(
      "--min-vmaf",
      &result.minVmaf,
      "minimum p5-VMAF quality floor for probing (0-100)",
      cfg::OptionalDefault{std::to_string(appctx::kDefaultMinVmaf)},
      cfg::ConfigKey{"min-vmaf"},
      cfg::TemplateKey{"min-vmaf"},
      cfg::Range{0, 100}
    ),
    opt(
      "--dry-run,--no-dry-run{false}",
      &result.dryRun,
      "probe and print the encoding plan, then exit without encoding",
      cfg::TemplateKey{"dry-run"},
      cfg::Excludes{"--crf"}
    ),
    opt(
      "--preset",
      &result.nvencPreset,
      "NVENC preset (p1-p7; auto picks by resolution)",
      cfg::RequiredDefault{"auto"},
      cfg::ConfigKey{"preset"},
      cfg::TemplateKey{"preset"},
      cfg::Members{"auto", "p1", "p2", "p3", "p4", "p5", "p6", "p7"}
    ),
    opt(
      "--video-codec",
      &result.videoCodec,
      "video encoder (default hevc_nvenc; libx265/libx264 on cpu)",
      cfg::DefaultValue{"hevc_nvenc"},
      cfg::ConfigKey{"video-codec"},
      cfg::TemplateKey{"video-codec"}
    ),
  };
  registerAll(processing, options, result.keyEntries, &result.templateEntries);
}

void registerFileOpFlags(CLI::App* fileop, CmdParseResult& result) {
  auto const options = std::tuple{
    opt(
      "-p,--pack,--no-pack{false}",
      &result.pack,
      "pack encoded video outputs into zip files",
      cfg::ConfigKey{"pack"},
      cfg::TemplateKey{"pack"},
      cfg::Excludes{"--pack-only"}
    ),
    opt(
      "-z,--pack-only,--no-pack-only{false}",
      &result.packOnly,
      "pack only: zip all files in input directory",
      cfg::TemplateKey{"pack-only"}
    ),
  };
  registerAll(fileop, options, result.keyEntries, &result.templateEntries);
}

// Batch keeps per-subfolder state files, so an explicit single state file has
// no meaning; the output aliases resolve against input lists batch does not
// have, so -o takes a plain directory path only.
auto batchOptionError(CmdParseResult const& result) -> std::optional<std::string> {
  if (result.stateFile.has_value()) {
    return std::string{
      "--state-file is not supported with batch: each subfolder keeps its own "
      "job state file"
    };
  }
  if (result.batchOutput.has_value() && cmd::isOutputAlias(result.batchOutput.value())) {
    return std::format(
      "output alias '{}' is not supported with batch: -o takes a plain directory "
      "path",
      result.batchOutput.value()
    );
  }
  return std::nullopt;
}

// Loads the user config, applies each stored value as a forced option default
// (design D1), then overlays the selected template on top of the same layer
// (command-templates design D2). Returns a load-error message, or nullopt.
auto injectStoredDefaults(CLI::App& app, CmdParseResult& result)
  -> std::optional<std::string> {
  auto const configPath = configstore::resolveConfigPath();
  auto const loaded = configstore::load(configPath, result.keyTable);
  if (loaded.error) { return loaded.error; }

  configstore::warnUnknownKeys(loaded, configPath);

  // One merged map: the template overwrites same-named config values, and
  // later default_str writes win, so the precedence chain lives in one place
  // (command line > template > config > built-in).
  auto values = loaded.values;
  auto const selected = result.selectedTemplate.value_or(std::string{});
  if (!selected.empty()) {
    auto const dir = templatestore::resolveTemplatesDir();
    auto const path = templatestore::find(dir, selected);
    if (!path.has_value()) { return templatestore::unknownTemplateMessage(selected); }

    auto const stored = templatestore::load(*path, result.templateTable);
    if (stored.error) { return stored.error; }
    configstore::warnUnknownKeys(stored, *path, "template key");
    for (auto const& [key, value]: stored.values) { values.insert_or_assign(key, value); }
    result.appliedTemplate = CmdParseResult::TemplateAttribution{
      .name = selected,
      .path = pathutf8::fromPath(*path),
    };
  }

  for (auto const& [key, value]: values) {
    if (auto* opt = app.get_option_no_throw(std::format("--{}", key))) {
      opt->default_str(value);
      opt->force_callback();
    }
    // Named subcommands may register their own copy of a config-key option
    // (preview's and batch's twins); get_option lookup only descends into
    // nameless groups, so sync the twins here. Display only, never
    // force_callback: the main option's forced callback already writes the
    // binding (registration order), and a forced subcommand callback would run
    // after an explicit main value and clobber it. Note: the no-arg
    // get_subcommands() returns parsed subcommands only (empty pre-parse); the
    // filter overload returns every registered subcommand.
    for (auto* subc: app.get_subcommands([](CLI::App*) { return true; })) {
      if (subc->get_name().empty()) { continue; }  // option groups: handled above
      if (auto* subOpt = subc->get_option_no_throw(std::format("--{}", key))) {
        subOpt->default_str(value);
      }
    }
  }

  // Sub-only config keys (model-dir lives on the organize subcommand, not
  // the parent app): inject as forced defaults on the sub's own option.
  // Safe against explicit CLI values: the callback reads parsed results and
  // only fires when that subcommand is parsed.
  for (auto const& [key, value]: values) {
    if (app.get_option_no_throw(std::format("--{}", key)) != nullptr) { continue; }
    for (auto* subc: app.get_subcommands([](CLI::App*) { return true; })) {
      if (subc->get_name().empty()) { continue; }
      if (auto* subOpt = subc->get_option_no_throw(std::format("--{}", key))) {
        subOpt->default_str(value);
        subOpt->force_callback();
      }
    }
  }
  return std::nullopt;
}

// Values `template save` should store: the templateable options this command
// line actually gave. Runs on the probe parse, so config-injected defaults are
// absent and the capture is pure command-line input. One walk over the main
// command surface does both jobs, because a template key is exactly its first
// long name without the dashes. A given option with no template key is an
// error rather than a silent drop.
auto captureTemplateValues(
  CLI::App& app,
  configstore::KeyTable const& table,
  std::map<std::string, std::string>& captured
) -> std::optional<std::string> {
  // The app's own options plus those of its nameless option groups
  // (get_option_no_throw descends into groups, the enumerating getters do not).
  auto options = app.get_options();
  for (auto* const group: app.get_subcommands([](CLI::App* sub) {
         return sub->get_name().empty();
       })) {
    for (auto* const opt: group->get_options()) { options.push_back(opt); }
  }

  for (auto* const opt: options) {
    if (opt->count() == 0) { continue; }
    auto const longName = cfg::captureLongName(opt);
    // A positional has no long name; it is still a given value, so it is
    // reported by its own name rather than skipped.
    auto const positional = std::string{opt->get_name(true)};
    auto const label = longName.value_or(positional);
    auto const key = longName.has_value() ? std::string_view{*longName}.substr(2)
                                          : std::string_view{positional};
    if (key == "help" || key == "version") { continue; }
    if (table.find(key) == nullptr) {
      return std::format(
        "{} cannot be stored in a template: it is not a templateable option",
        label
      );
    }
    auto const& results = opt->results();
    if (results.empty()) { continue; }
    captured.insert_or_assign(std::string{key}, results.back());
  }

  if (captured.empty()) {
    return std::string{
      "nothing to save: pass at least one templateable option (for example "
      "--crf 24)"
    };
  }
  return std::nullopt;
}

auto buildAndParse(
  int argc,
  char* argv[],
  std::string const& introLine,
  bool injectConfig,
  std::string const& selectedTemplate
) -> CmdParseResult {
  auto result = CmdParseResult{};
  auto tree = buildAppTree(result, introLine, injectConfig, selectedTemplate);
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
    // -T takes an optional name: an empty result means the flag carried none,
    // which asks for the template list rather than a run.
    if (
      auto const* const templateOpt = tree.app->get_option_no_throw("--template");
      templateOpt != nullptr && templateOpt->count() > 0
    ) {
      result.templateListRequested =
        !result.selectedTemplate.has_value() || result.selectedTemplate->empty();
    }
    if (tree.app->got_subcommand(tree.previewSub)) { result.preview = true; }
    if (tree.app->got_subcommand(tree.organizeSub)) {
      result.organize = true;
      if (auto const error = organizeDirError(result); error.has_value()) {
        result.error = *error;
      } else if (result.organizeDir.has_value()) {
        // Folder-flag names are validated before any scan, through the same
        // argument-error channel as the missing directory (spec "Command
        // surface and scanning").
        if (
          auto const error = organize::validateDispositionFlags(
            fs::path{*result.organizeDir},
            result.organizeIngest.value_or(std::vector<std::string>{}),
            result.organizeIgnoreFolder.value_or(std::vector<std::string>{})
          );
          error.has_value()
        ) {
          result.error = *error;
        }
      }
    }
    if (tree.app->got_subcommand(tree.batchSub)) {
      result.batch = true;
      if (auto const error = batchOptionError(result); error.has_value()) {
        result.error = *error;
      }
    }
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
    if (tree.app->got_subcommand(tree.templateSub)) {
      result.templates = true;
      result.helpApp_ = tree.templateSub;
      if (auto const error = templateActionArityError(result); error.has_value()) {
        result.error = *error;
      } else if (result.templateVerb == "save" && !injectConfig) {
        // Captured from the probe parse only (see 5.x): the check and the
        // collection share one walk, so a rejected save writes nothing.
        if (
          auto const error = captureTemplateValues(
            *tree.app,
            result.templateTable,
            result.capturedTemplateValues
          );
          error.has_value()
        ) {
          result.error = *error;
        }
      }
    }
  } catch (CLI::CallForHelp const&) {
    // The preview/config/completion subcommands carry the native help flags
    // (the parent app cleared its own), so the help text comes from whichever
    // matched.
    result.help = true;
    if (tree.app->got_subcommand(tree.completionSub)) {
      result.helpApp_ = tree.completionSub;
    } else if (tree.app->got_subcommand(tree.templateSub)) {
      result.helpApp_ = tree.templateSub;
    } else if (tree.app->got_subcommand(tree.configSub)) {
      result.helpApp_ = tree.configSub;
    } else if (tree.app->got_subcommand(tree.batchSub)) {
      result.helpApp_ = tree.batchSub;
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

auto buildAppTree(
  CmdParseResult& result,
  std::string const& introLine,
  bool injectConfig,
  std::string const& selectedTemplate
) -> AppTree {
  // The probe parse already resolved the name; injection runs before this
  // parse repopulates the binding, so seed it here (design D3).
  if (!selectedTemplate.empty()) { result.selectedTemplate = selectedTemplate; }
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
  auto* batchSub = registerBatchSubcommand(*app, result);
  auto* configSub = registerConfigSubcommand(*app, result);
  auto* completionSub = registerCompletionSubcommand(*app, result);
  auto* templateSub = registerTemplateSubcommand(*app, result);
  // Option groups are CLI11 subcommands too, so the commands section is given
  // the real subcommand apps explicitly. The array is leaked with the app:
  // the formatter lambda captures the span by value, while parse and help
  // rendering happen after this call returns.
  auto* const commandSubs = new std::array<CLI::App const*, 6>{
    previewSub,
    organizeSub,
    batchSub,
    configSub,
    completionSub,
    templateSub
  };

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

  // ── Template-key table: assembled from the same registration capture. A
  // marker that drifts from the template order fails the run here.
  auto const templateTable = configstore::assembleKeyTable(
    configstore::canonicalTemplateKeyOrder(),
    result.templateEntries,
    "template key table"
  );
  if (templateTable.has_value()) {
    result.templateTable = *templateTable;
    // Completion offers the templateable keys at the `template` key positional
    // (complete-template-names design D5). Sorted, so the embedded candidate
    // list is stable rather than following the table's write order.
    if (auto* keyOpt = templateSub->get_option_no_throw("key"); keyOpt != nullptr) {
      auto keys = std::vector<std::string>{};
      keys.reserve(result.templateTable.keys.size());
      for (auto const& def: result.templateTable.keys) { keys.emplace_back(def.key); }
      std::ranges::sort(keys);
      completion::recordPositional(keyOpt, std::move(keys));
    }
  } else if (!result.error.has_value()) {
    result.error = templateTable.error();
  }

  // ── Config injection (design D1): config values become forced option
  // defaults, so CLI values win, validators run on applied defaults, and the
  // help (=default) display shows effective defaults. A failed table already
  // failed the run, so it skips injection.
  if (injectConfig && !result.error.has_value()) {
    if (auto const error = injectStoredDefaults(*app, result); error.has_value()) {
      result.error = *error;
    }
  }
  return AppTree{
    app,
    previewSub,
    organizeSub,
    batchSub,
    configSub,
    completionSub,
    templateSub
  };
}

auto commandLineInit(int argc, char* argv[], std::string const& introLine)
  -> CmdParseResult {
  // Probe parse without config injection: config-subcommand actions must
  // operate on stores holding invalid values (e.g. `config unset` of a bad
  // key), and pure CLI errors surface unchanged. Every other path re-parses
  // with injection so option defaults and the help (=default) display reflect
  // the config.
  auto probe = buildAndParse(argc, argv, introLine, false, {});

  // The template actions never read stored defaults, so a selected template
  // has nothing to shape there: reject it instead of silently ignoring it.
  auto const wantsTemplate =
    probe.selectedTemplate.has_value() || probe.templateListRequested;
  if (wantsTemplate && (probe.config || probe.completion || probe.templates)) {
    auto const* const other = probe.config ? "config"
      : probe.completion                   ? "completion"
                                           : "template";
    probe.error = std::format("--template has no effect with the {} subcommand", other);
    return probe;
  }

  // A name-less -T asks for the template list: the same action as
  // `encro template list`, and just as free of stored defaults.
  if (probe.templateListRequested) {
    probe.templates = true;
    probe.templateVerb = "list";
  }

  // completion joins config here: emission never reads the stored config, so
  // a corrupt config file must not fail it (add-shell-completion spec). The
  // template actions join them for the same reason: they operate on the
  // template store, never on the injected defaults.
  if (probe.error.has_value() || probe.config || probe.completion || probe.templates) {
    return probe;
  }
  return buildAndParse(
    argc,
    argv,
    introLine,
    true,
    probe.selectedTemplate.value_or(std::string{})
  );
}
