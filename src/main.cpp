#include "cmd/cmd.h"
#include "cmd/config_builder.h"
#include "core/app_context.h"
#include "core/pipeline.h"
#include "core/toolchain.h"

#include <spdlog/spdlog.h>

#include <iostream>

int main(int argc, char* argv[]) {
  auto [desc, vm] = commandLineInit(argc, argv);
  spdlog::set_pattern("[%^%l%$] %v");

  if (vm.count("help")) {
    desc.print(std::cout);
    return 0;
  }

  if (vm.count("verbose")) {
    spdlog::set_level(spdlog::level::debug);
    spdlog::debug("Verbose logging enabled.");
  }

  auto configRes = cmd::buildConfig(vm);
  if (!configRes) {
    spdlog::error(configRes.error());
    std::cout << desc << "\n";
    return 1;
  }

  auto config = std::move(configRes.value());

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

  auto ctx = appctx::AppContext{.config = config};

  if (!ctx.config.packOnly) {
    auto const toolRes = toolchain::resolve(ctx.config, ctx.toolchain);
    if (!toolRes) {
      spdlog::error("Tool check failed: {}", toolRes.error());
      return 1;
    }
  }

  auto runRes = pipeline::run(ctx);
  if (!runRes) {
    spdlog::error("Pipeline failed: {}", runRes.error());
    return 1;
  }

  return runRes.value();
}
