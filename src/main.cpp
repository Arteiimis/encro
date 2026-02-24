#include "cmd.h"
#include "globals.h"
#include "packer.h"
#include "picture_process.h"
#include "utils.h"
#include "video_process.h"

#include <spdlog/spdlog.h>

#include <iostream>
#include <print>


int main(int argc, char* argv[]) {
  auto [desc, vm] = commandLineInit(argc, argv);
  spdlog::set_pattern("[%^%l%$] %v");
  GLBs.OUTPUT_FORMAT = "mp4";
  GLBs.PACK_OUTPUT = false;
  GLBs.PACK_ONLY = false;

  if (vm.count("help")) {
    desc.print(std::cout);
    return 0;
  }

  if (vm.count("type")) {
    const auto typeStr = getParamStr(vm, "type");
    constexpr auto validTypes = std::array{"video", "picture"};
    if (!std::ranges::contains(validTypes, typeStr)) {
      spdlog::error(
        "Invalid process type: {}. Valid types are: video, picture.",
        typeStr
      );
      return 1;
    }
    GLBs.PROCESS_TYPE = typeStr;
  }

  if (vm.count("verbose")) {
    spdlog::set_level(spdlog::level::debug);
    spdlog::debug("Verbose logging enabled.");
  }

  if (vm.count("yes")) {
    GLBs.YES_TO_ALL = true;
    spdlog::info("Automatic 'yes to all' enabled.");
  }

  if (vm.count("recursive")) {
    GLBs.RECURSIVE = true;
    spdlog::info("Recursive directory search enabled.");
  }

  if (vm.count("pack")) {
    GLBs.PACK_OUTPUT = true;
    spdlog::info("Pack output enabled for video processing.");
  }

  if (vm.count("pack-only")) {
    GLBs.PACK_ONLY = true;
    spdlog::info("Pack-only mode enabled.");
  }

  if (vm.count("ffmpeg-path")) {
    const auto iptPath = fs::path{getParamStr(vm, "ffmpeg-path")};

    if (!fs::is_directory(iptPath)) {
      spdlog::error("The specified FFmpeg path is invalid: {}", iptPath.string());
      return 1;
    }

    GLBs.FFMPEG_INSTALL_DIR = iptPath;

    spdlog::info(
      "Using custom FFmpeg install directory: {}",
      GLBs.FFMPEG_INSTALL_DIR.value().string()
    );
  }

  if (!vm.count("input")) {
    spdlog::error("Input path is required.");
    std::cout << desc << "\n";
    return 1;
  }

  if (vm.count("output")) {
    GLBs.OUTPUT_PATH = fs::path{getParamStr(vm, "output")};

    if (!fs::is_directory(GLBs.OUTPUT_PATH.value())) {
      spdlog::error(
        "The specified output path is not a directory: {}",
        GLBs.OUTPUT_PATH.value().string()
      );
      return 1;
    }

    spdlog::info("Using custom output path: {}", GLBs.OUTPUT_PATH.value().string());
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

    GLBs.OUTPUT_FORMAT = outputFormat;
  }

  GLBs.INPUT_PATH = fs::path{getParamStr(vm, "input")};

  if (!fs::exists(GLBs.INPUT_PATH)) {
    spdlog::error(
      "The specified path/file does not exist: {}",
      GLBs.INPUT_PATH.string()
    );
    return 1;
  }

  if (GLBs.PACK_ONLY) {
    if (GLBs.PROCESS_TYPE != "video") {
      spdlog::error("pack-only option is only supported when --type is video.");
      return 1;
    }

    if (!fs::is_directory(GLBs.INPUT_PATH)) {
      spdlog::error("pack-only mode requires input to be a directory.");
      return 1;
    }

    auto const zipOutputDir = GLBs.OUTPUT_PATH.value_or(GLBs.INPUT_PATH / "packed");
    auto const packRes = packAllFilesInDirectory(
      GLBs.INPUT_PATH,
      zipOutputDir,
      500 * 1024 * 1024,
      true
    );

    if (!packRes) {
      spdlog::error("Failed to pack files: {}", packRes.error());
      return 1;
    }

    std::println("All files packed successfully to: {}", zipOutputDir.string());
    return 0;
  }

  if (auto const toolRes = toolCheck(); !toolRes) {
    spdlog::error("Tool check failed: {}", toolRes.error());
    return 1;
  }

  if (GLBs.PROCESS_TYPE == "video" && fs::is_directory(GLBs.INPUT_PATH)) {
    return handlePathEncoding(GLBs.INPUT_PATH);
  }

  if (GLBs.PROCESS_TYPE == "video" && fs::is_regular_file(GLBs.INPUT_PATH)) {
    return handleSingleFileEncoding(GLBs.INPUT_PATH);
  }

  if (GLBs.PROCESS_TYPE == "picture" && fs::is_directory(GLBs.INPUT_PATH)) {
    const auto outputDir = GLBs.OUTPUT_PATH.value_or(GLBs.INPUT_PATH) / "packed";
    const auto packRes = packAllPicsToZipParallel(GLBs.INPUT_PATH, outputDir);

    if (!packRes) {
      spdlog::error("Failed to pack pictures: {}", packRes.error());
      return 1;
    }

    std::println("All pictures packed successfully to: {}", outputDir.string());

    return 0;
  }

  spdlog::error(
    "The specified path is neither a directory nor a regular file: {}",
    GLBs.INPUT_PATH.string()
  );

  return 1;
}
