#include "app/prelude.h"

#include "infra/terminal.h"
#include "logging/log_tags.h"
#include "logging/logging.h"
#include "logging/setup.h"

#include <spdlog/spdlog.h>

#include <filesystem>

namespace fs = std::filesystem;

using enum terminal::MessageKind;

namespace {

auto setupLogging(CmdParseResult const& cmd) -> std::optional<fs::path> {
  if (!cmd.verbose) {
    spdlog::set_level(spdlog::level::off);
    if (cmd.verboseEcho) {
      terminal::println(
        Warning,
        "Warning: --verbose-echo requires --verbose; option ignored."
      );
    }
    return std::nullopt;
  }

  auto const logConfig = logging::LogConfig{
    .verboseEnabled = cmd.verbose,
    .verboseEchoEnabled = cmd.verboseEcho,
    .colorsEnabled = terminal::colorsEnabled(),
  };

  auto const logFilePath = logging::setup(logConfig);

  if (logFilePath.has_value() && !cmd.verboseEcho) {
    terminal::println(Hint, "Verbose log file: {}", terminal::path(logFilePath.value()));
  }

  return logFilePath;
}

}  // namespace

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

  auto verboseLogFilePath = setupLogging(cmd);
  return StartupContext{std::move(cmd), std::move(verboseLogFilePath)};
}

void printVerboseLogDirHint(
  std::optional<std::filesystem::path> const& verboseLogFilePath
) {
  if (!verboseLogFilePath.has_value()) { return; }
  terminal::println(
    Hint,
    "Verbose log directory: {}",
    terminal::path(verboseLogFilePath->parent_path())
  );
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
      config.ffmpegInstallDir.value().string()
    );
  }

  if (config.outputPath.has_value()) {
    LOG_INFO("Using custom output path: {}", config.outputPath.value().string());
  }

  if (config.stateFilePath.has_value()) {
    LOG_INFO("Using custom state file: {}", config.stateFilePath.value().string());
  }

  if (config.maxParallelJobs.has_value()) {
    LOG_INFO("Using custom max parallel jobs: {}", config.maxParallelJobs.value());
  }
}

}  // namespace prelude
