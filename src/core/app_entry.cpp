#include "core/app_entry.h"

#include "cmd/config_builder.h"
#include "core/app_context.h"
#include "core/pipeline.h"
#include "core/prelude.h"
#include "core/toolchain.h"

#include <spdlog/spdlog.h>

#include <array>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

auto parseDecimal(std::string_view text) -> int {
  auto value = 0;
  for (char const ch: text) {
    if (ch == ' ') { continue; }
    value = value * 10 + (ch - '0');
  }
  return value;
}

auto monthNumber(std::string_view month) -> int {
  using namespace std::literals;
  constexpr auto months = std::array{
    "Jan"sv,
    "Feb"sv,
    "Mar"sv,
    "Apr"sv,
    "May"sv,
    "Jun"sv,
    "Jul"sv,
    "Aug"sv,
    "Sep"sv,
    "Oct"sv,
    "Nov"sv,
    "Dec"sv,
  };

  for (auto index = std::size_t{0}; index < months.size(); ++index) {
    if (months[index] == month) { return static_cast<int>(index) + 1; }
  }

  return 0;
}

auto compileTimestamp() -> std::string {
  constexpr auto buildDate = std::string_view{__DATE__};
  constexpr auto buildTime = std::string_view{__TIME__};

  auto const year = parseDecimal(buildDate.substr(7, 4));
  auto const month = monthNumber(buildDate.substr(0, 3));
  auto const day = parseDecimal(buildDate.substr(4, 2));

  return std::format("{:04d}-{:02d}-{:02d} {}", year, month, day, buildTime);
}

auto printHelp(std::ostream& out, CmdParseResult const& cmd) -> void {
  out << appentry::helpIntroLine() << "\n\n";
  cmd.desc.print(out);
}

auto failWithHint(
  prelude::StartupContext const& startup,
  std::string const& message,
  bool showHelp = false
) -> int {
  if (startup.verboseLogFilePath.has_value()) {
    spdlog::error("{}", message);
  } else {
    std::cout << "Error: " << message << "\n";
  }
  prelude::printVerboseLogDirHint(startup.verboseLogFilePath);
  if (showHelp) { printHelp(std::cout, startup.cmd); }
  return 1;
}

auto handleParseAndHelp(prelude::StartupContext const& startup)
  -> std::optional<int> {
  auto const& [desc, vm, error] = startup.cmd;

  if (error.has_value()) {
    return failWithHint(
      startup,
      std::format("Invalid arguments: {}", error.value()),
      true
    );
  }

  if (vm.count("help")) {
    printHelp(std::cout, startup.cmd);
    return 0;
  }

  return std::nullopt;
}

auto buildAppConfig(prelude::StartupContext const& startup)
  -> std::optional<appctx::AppConfig> {
  auto const& vm = startup.cmd.vm;

  auto configRes = cmd::buildConfig(vm);
  if (!configRes) {
    failWithHint(startup, configRes.error(), true);
    return std::nullopt;
  }

  auto config = std::move(configRes.value());
  prelude::logConfigSummary(config);
  return config;
}

auto ensureToolchainReady(
  appctx::AppContext& ctx,
  prelude::StartupContext const& startup
) -> bool {
  if (ctx.config.packOnly) { return true; }

  auto const toolRes = toolchain::resolve(ctx.config, ctx.toolchain);
  if (!toolRes) {
    failWithHint(startup, std::format("Tool check failed: {}", toolRes.error()));
    return false;
  }

  return true;
}

auto runAppPipeline(appctx::AppContext& ctx, prelude::StartupContext const& startup)
  -> int {
  auto runRes = pipeline::run(ctx);
  if (!runRes) {
    return failWithHint(startup, std::format("Pipeline failed: {}", runRes.error()));
  }

  return runRes.value();
}

}  // namespace

namespace appentry {

auto helpIntroLine() -> std::string {
  return std::format(
    "encro: Universal video encoder/converter/packer | build: {}",
    compileTimestamp()
  );
}

auto run(int argc, char* argv[]) -> int {
  auto const startup = prelude::initStartup(argc, argv);

  if (auto const earlyExit = handleParseAndHelp(startup); earlyExit.has_value()) {
    return earlyExit.value();
  }

  auto config = buildAppConfig(startup);
  if (!config.has_value()) { return 1; }

  auto ctx = appctx::AppContext{.config = std::move(config.value())};

  if (!ensureToolchainReady(ctx, startup)) { return 1; }

  return runAppPipeline(ctx, startup);
}

}  // namespace appentry
