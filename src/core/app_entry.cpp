#include "core/app_entry.h"

#include "cmd/config_builder.h"
#include "core/app_context.h"
#include "core/pipeline.h"
#include "core/prelude.h"
#include "core/toolchain.h"

#include <spdlog/spdlog.h>

#include <format>
#include <iostream>
#include <optional>

namespace {

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
  if (showHelp) { startup.cmd.desc.print(std::cout); }
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
    desc.print(std::cout);
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
