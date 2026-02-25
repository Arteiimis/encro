#include "cmd/cmd.h"
#include "core/app_context.h"
#include "core/pipeline.h"
#include "core/toolchain.h"
#include "utils/utils.h"

#include <spdlog/spdlog.h>

#include <iostream>

int main(int argc, char* argv[]) {
  auto [desc, vm] = commandLineInit(argc, argv);
  spdlog::set_pattern("[%^%l%$] %v");
  auto config = appctx::AppConfig{};

  if (vm.count("help")) {
    desc.print(std::cout);
    return 0;
  }

  if (vm.count("type")) {
    auto const typeStr = getParamStr(vm, "type");
    constexpr auto validTypes = std::array{"video", "picture"};
    if (!std::ranges::contains(validTypes, typeStr)) {
      spdlog::error(
        "Invalid process type: {}. Valid types are: video, picture.",
        typeStr
      );
      return 1;
    }
    config.processType = typeStr;
  }

  if (vm.count("verbose")) {
    spdlog::set_level(spdlog::level::debug);
    spdlog::debug("Verbose logging enabled.");
  }

  if (vm.count("yes")) {
    config.yesToAll = true;
    spdlog::info("Automatic 'yes to all' enabled.");
  }

  if (vm.count("recursive")) {
    config.recursive = true;
    spdlog::info("Recursive directory search enabled.");
  }

  if (vm.count("pack")) {
    config.packOutput = true;
    spdlog::info("Pack output enabled for video processing.");
  }

  if (vm.count("pack-only")) {
    config.packOnly = true;
    spdlog::info("Pack-only mode enabled.");
  }

  if (vm.count("ffmpeg-path")) {
    auto const iptPath = fs::path{getParamStr(vm, "ffmpeg-path")};

    if (!fs::is_directory(iptPath)) {
      spdlog::error("The specified FFmpeg path is invalid: {}", iptPath.string());
      return 1;
    }

    config.ffmpegInstallDir = iptPath;

    spdlog::info(
      "Using custom FFmpeg install directory: {}",
      config.ffmpegInstallDir.value().string()
    );
  }

  if (!vm.count("input")) {
    spdlog::error("Input path is required.");
    std::cout << desc << "\n";
    return 1;
  }

  if (vm.count("output")) {
    config.outputPath = fs::path{getParamStr(vm, "output")};

    if (!fs::is_directory(config.outputPath.value())) {
      spdlog::error(
        "The specified output path is not a directory: {}",
        config.outputPath.value().string()
      );
      return 1;
    }

    spdlog::info("Using custom output path: {}", config.outputPath.value().string());
  }

  if (vm.count("output-format")) {
    auto const outputFormat = getParamStr(vm, "output-format");
    constexpr auto validFormats = std::array{"mp4", "webp"};

    if (!std::ranges::contains(validFormats, outputFormat)) {
      spdlog::error(
        "Invalid output format: {}. Valid formats are: mp4, webp.",
        outputFormat
      );
      return 1;
    }

    config.outputFormat = outputFormat;
  }

  config.inputPath = fs::path{getParamStr(vm, "input")};

  if (!fs::exists(config.inputPath)) {
    spdlog::error(
      "The specified path/file does not exist: {}",
      config.inputPath.string()
    );
    return 1;
  }

  auto ctx = appctx::AppContext{.config = config};

  if (!ctx.config.packOnly) {
    if (auto const toolRes = toolchain::resolve(ctx.config, ctx.toolchain);
        !toolRes) {
      spdlog::error("Tool check failed: {}", toolRes.error());
      return 1;
    }
  }

  auto pipelineRes = pipeline::selectPipeline(ctx);
  if (!pipelineRes) {
    spdlog::error("Pipeline selection failed: {}", pipelineRes.error());
    return 1;
  }

  auto runRes = pipelineRes.value()->run(ctx);
  if (!runRes) {
    spdlog::error("Pipeline failed: {}", runRes.error());
    return 1;
  }

  return runRes.value();
}
