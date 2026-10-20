#include "app/prelude.h"
#include "core/path_utf8.h"

#include "core/work_dirs.h"
#include "infra/terminal.h"
#include "logging/log_tags.h"
#include "logging/logging.h"
#include "logging/setup.h"

namespace {

void setupLogging(CmdParseResult const& cmd) {
  auto const logConfig = logging::LogConfig{
    .echoLevel = cmd.verbosity,
    .jsonEnabled = cmd.jsonEnabled,
    .colorsEnabled = terminal::colorsEnabled(),
  };
  terminal::setQuiet(cmd.quiet);

  // The log path is surfaced by logging::printLogHint on failed runs.
  (void)logging::setup(logConfig);
}

}  // namespace

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::APP_PRELUDE);

namespace prelude {

auto initStartup(int argc, char* argv[], std::string const& introLine) -> StartupContext {
  auto cmd = commandLineInit(argc, argv, introLine);

  if (
    auto const terminalError = terminal::configureFromColorString(cmd.color);
    terminalError.has_value() && !cmd.error.has_value()
  ) {
    cmd.error = terminalError;
  }

  // Log even on failed runs whose cmd.error was set after parse (e.g. invalid
  // --color with -h), so the failure lands in the log file, not stderr only.
  auto loggingActive = false;
  if ((!cmd.help && !cmd.version) || cmd.error.has_value()) {
    setupLogging(cmd);
    loggingActive = true;
  }

  // After setupLogging, so the line follows the run's echo level (-v) and
  // --quiet (design D8).
  logTemplateAttribution(cmd);

  // Reclaim leftovers from crashed runs: stale per-run scratch entries older
  // than 24h are removed (design D3). Fresh files belong to live runs and are
  // never touched.
  workdirs::sweepScratchDir();

  return StartupContext{
    .cmd = std::move(cmd),
    .loggingActive = loggingActive,
  };
}

void logConfigSummary(appctx::AppConfig const& config) {
  if (config.yesToAll) { LOG_INFO("Automatic 'yes to all' enabled."); }

  if (config.recursive) { LOG_INFO("Recursive directory search enabled."); }

  if (config.packOutput) { LOG_INFO("Pack output enabled for video processing."); }

  if (config.packOnly) { LOG_INFO("Pack-only mode enabled."); }

  if (config.resumeState) { LOG_INFO("Resume mode enabled."); }

  if (config.restartState) { LOG_INFO("Restart mode enabled."); }

  if (!config.forceNameConflictHandling) {
    LOG_INFO("Collision-safe file naming disabled for unique flat outputs.");
  }

  if (config.pictureFolderSummary) { LOG_INFO("Picture folder summary images enabled."); }

  if (config.ffmpegInstallDir.has_value()) {
    LOG_INFO(
      "Using custom FFmpeg install directory: {}",
      pathutf8::fromPath(config.ffmpegInstallDir.value())
    );
  }

  if (config.outputPath.has_value()) {
    LOG_INFO(
      "Using custom output path: {}",
      pathutf8::fromPath(config.outputPath.value())
    );
  }

  if (config.stateFilePath.has_value()) {
    LOG_INFO(
      "Using custom state file: {}",
      pathutf8::fromPath(config.stateFilePath.value())
    );
  }

  if (config.maxParallelJobs.has_value()) {
    LOG_INFO("Using custom max parallel jobs: {}", config.maxParallelJobs.value());
  }
}

void logTemplateAttribution(CmdParseResult const& cmd) {
  if (!cmd.appliedTemplateName.has_value()) { return; }
  LOG_INFO(
    "Applied template '{}' from {}.",
    *cmd.appliedTemplateName,
    cmd.appliedTemplatePath
  );
}

}  // namespace prelude
