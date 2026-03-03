#include "core/prelude.h"

#include <spdlog/async.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <memory>
#include <mutex>
#include <print>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

#if defined(_WIN32) || defined(_WIN64)
auto readWindowsEnvPath(char const* name) -> std::optional<fs::path> {
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, name) != 0 || value == nullptr || size == 0) {
    return std::nullopt;
  }

  auto result = fs::path{value};
  std::free(value);
  if (result.empty()) { return std::nullopt; }

  return result;
}
#endif

auto resolveCommonLogDir() -> fs::path {
#if defined(_WIN32) || defined(_WIN64)
  if (
    auto const localAppData = readWindowsEnvPath("LOCALAPPDATA");
    localAppData.has_value()
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

auto setupLogging(po::variables_map const& vm) -> std::optional<fs::path> {
  constexpr auto kLogPattern = "[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] %v";
  spdlog::set_pattern(kLogPattern);

  auto const verboseEnabled = vm.count("verbose") > 0;
  auto const verboseEchoEnabled = vm.count("verbose-echo") > 0;

  if (!verboseEnabled) {
    spdlog::set_level(spdlog::level::off);
    if (verboseEchoEnabled) {
      std::println("Warning: --verbose-echo requires --verbose; option ignored.");
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
    sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
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
    std::println("Verbose log file: {}", logFilePath.string());
  }

  return logFilePath;
}

}  // namespace

namespace prelude {

auto initStartup(int argc, char* argv[]) -> StartupContext {
  auto cmd = commandLineInit(argc, argv);
  auto verboseLogFilePath = setupLogging(cmd.vm);
  return StartupContext{std::move(cmd), std::move(verboseLogFilePath)};
}

void printVerboseLogDirHint(
  std::optional<std::filesystem::path> const& verboseLogFilePath
) {
  if (!verboseLogFilePath.has_value()) { return; }
  std::println(
    "Verbose log directory: {}",
    verboseLogFilePath->parent_path().string()
  );
}

void logConfigSummary(appctx::AppConfig const& config) {
  if (config.yesToAll) { spdlog::info("Automatic 'yes to all' enabled."); }

  if (config.recursive) { spdlog::info("Recursive directory search enabled."); }

  if (config.packOutput) {
    spdlog::info("Pack output enabled for video processing.");
  }

  if (config.packOnly) { spdlog::info("Pack-only mode enabled."); }

  if (config.ffmpegInstallDir.has_value()) {
    spdlog::info(
      "Using custom FFmpeg install directory: {}",
      config.ffmpegInstallDir.value().string()
    );
  }

  if (config.outputPath.has_value()) {
    spdlog::info("Using custom output path: {}", config.outputPath.value().string());
  }
}

}  // namespace prelude
