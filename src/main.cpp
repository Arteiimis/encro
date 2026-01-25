#include <iostream>
#include <print>

#include <spdlog/spdlog.h>

#include "cmd.h"
#include "globals.h"
#include "utils.h"
#include "video_process.h"
#include "picture_process.h"

int main(int argc, char* argv[]) {
  auto [desc, vm] = commandLineInit(argc, argv);
  spdlog::set_pattern("[%^%l%$] %v");

  if (vm.count("help")) {
    std::cout << desc << "\n";
    return 0;
  }

  if (vm.count("type")) {
    const auto typeStr = boost::trim_copy(vm.at("type").as<std::string>());
    if (typeStr == std::string("video")) {
      GLBs.PROCESS_TYPE = "video";
    } else if (typeStr == std::string("picture")) {
      GLBs.PROCESS_TYPE = "picture";
    } else {
      spdlog::error(
        "Invalid process type specified: {}",
        vm.at("type").as<std::string>()
      );
      return 1;
    }
  } else {
    GLBs.PROCESS_TYPE = "video";
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

  if (vm.count("ffmpeg-path")) {
    const auto iptPath = fs::path{
      boost::trim_copy(vm.at("ffmpeg-path").as<std::string>())
    };

    if (!fs::is_directory(iptPath) && !fs::is_regular_file(iptPath)) {
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
    GLBs.OUTPUT_PATH = fs::path{boost::trim_copy(vm.at("output").as<std::string>())};

    if (!fs::is_directory(GLBs.OUTPUT_PATH.value())) {
      spdlog::error(
        "The specified output path is not a directory: {}",
        GLBs.OUTPUT_PATH.value().string()
      );
      return 1;
    }

    spdlog::info("Using custom output path: {}", GLBs.OUTPUT_PATH.value().string());
  }

  if (!toolCheck()) { return 1; }

  GLBs.INPUT_PATH = fs::path{boost::trim_copy(vm.at("input").as<std::string>())};

  if (!fs::exists(GLBs.INPUT_PATH)) {
    spdlog::error(
      "The specified path/file does not exist: {}",
      GLBs.INPUT_PATH.string()
    );
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
    const auto packRes   = packAllPicsToZipParallel(GLBs.INPUT_PATH, outputDir);

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
