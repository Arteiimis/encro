#include "app/prelude.h"

#include "infra/terminal.h"

#include <spdlog/async.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <memory>
#include <mutex>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

using enum terminal::MessageKind;

namespace {

#if defined(_WIN32) || defined(_WIN64)
auto readWindowsEnvPath(char const* name) -> std::optional<fs::path> {
  auto value = std::unique_ptr<char>{};
  auto size = std::size_t{0};
  if (_dupenv_s(std::out_ptr(value), &size, name) != 0 || value == nullptr || size == 0) {
    return std::nullopt;
  }

  auto result = fs::path{value.get()};
  if (result.empty()) { return std::nullopt; }

  return result;
}
#endif

auto resolveCommonLogDir() -> fs::path {
#if defined(_WIN32) || defined(_WIN64)
  if (
    auto const localAppData = readWindowsEnvPath("LOCALAPPDATA"); localAppData.has_value()
  ) {
    return localAppData.value() / "encro" / "logs";
  }

  if (auto const appData = readWindowsEnvPath("APPDATA"); appData.has_value()) {
    return appData.value() / "encro" / "logs";
  }
#else
  if (auto const* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return fs::path{home} / ".local" / "state" / "encro" / "logs";
  }
#endif

  return fs::temp_directory_path() / "encro" / "logs";
}

auto setupLogging(CmdParseResult const& cmd) -> std::optional<fs::path> {
  constexpr auto kLogPattern = "[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] %v";
  spdlog::set_pattern(kLogPattern);

  auto const verboseEnabled = cmd.verbose;
  auto const verboseEchoEnabled = cmd.verboseEcho;

  if (!verboseEnabled) {
    spdlog::set_level(spdlog::level::off);
    if (verboseEchoEnabled) {
      terminal::println(
        Warning,
        "Warning: --verbose-echo requires --verbose; option ignored."
      );
    }
    return std::nullopt;
  }

  auto logDir = resolveCommonLogDir();
  auto ec = std::error_code{};
  fs::create_directories(logDir, ec);
  if (ec) {
    spdlog::warn(
      "Cannot create log directory '{}': {}. Fallback to temp directory.",
      logDir.string(),
      ec.message()
    );
    logDir = fs::temp_directory_path() / "encro" / "logs";
    fs::create_directories(logDir, ec);
  }

  auto const logFilePath = logDir / "encro.verbose.log";

  auto sinks = std::vector<spdlog::sink_ptr>{};
  sinks.emplace_back(
    std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true)
  );

  if (verboseEchoEnabled) {
    if (terminal::colorsEnabled()) {
      sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    } else {
      sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());
    }
  }

  static auto poolInitFlag = std::once_flag{};
  std::call_once(poolInitFlag, [] { spdlog::init_thread_pool(8192, 1); });

  auto logger = std::make_shared<spdlog::async_logger>(
    "encro",
    sinks.begin(),
    sinks.end(),
    spdlog::thread_pool(),
    spdlog::async_overflow_policy::block
  );
  logger->set_pattern(kLogPattern);
  logger->set_level(spdlog::level::debug);
  logger->flush_on(spdlog::level::err);

  spdlog::set_default_logger(std::move(logger));
  spdlog::set_level(spdlog::level::debug);

  spdlog::debug("Verbose logging enabled.");
  if (!verboseEchoEnabled) {
    terminal::println(Hint, "Verbose log file: {}", terminal::path(logFilePath));
  }

  return logFilePath;
}

}  // namespace

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
  if (config.yesToAll) { spdlog::info("Automatic 'yes to all' enabled."); }

  if (config.recursive) { spdlog::info("Recursive directory search enabled."); }

  if (config.packOutput) { spdlog::info("Pack output enabled for video processing."); }

  if (config.packOnly) { spdlog::info("Pack-only mode enabled."); }

  if (config.resumeState) { spdlog::info("Resume mode enabled."); }

  if (config.restartState) { spdlog::info("Restart mode enabled."); }

  if (!config.forceNameConflictHandling) {
    spdlog::info("Collision-safe file naming disabled for unique flat outputs.");
  }

  if (config.pictureFolderSummary) {
    spdlog::info("Picture folder summary images enabled.");
  }

  if (config.ffmpegInstallDir.has_value()) {
    spdlog::info(
      "Using custom FFmpeg install directory: {}",
      config.ffmpegInstallDir.value().string()
    );
  }

  if (config.outputPath.has_value()) {
    spdlog::info("Using custom output path: {}", config.outputPath.value().string());
  }

  if (config.stateFilePath.has_value()) {
    spdlog::info("Using custom state file: {}", config.stateFilePath.value().string());
  }

  if (config.maxParallelJobs.has_value()) {
    spdlog::info("Using custom max parallel jobs: {}", config.maxParallelJobs.value());
  }
}

}  // namespace prelude
