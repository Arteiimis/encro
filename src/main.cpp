#include <iostream>
#include <print>

#include "cmd.h"
#include "globals.h"
#include "utils.h"
#include "video_process.h"

int main(int argc, char* argv[]) {
  auto [desc, vm] = commandLineInit(argc, argv);

  if (vm.count("help")) {
    std::cout << desc << "\n";
    return 0;
  }

  if (vm.count("ffmpeg-path")) {
    const auto iptPath = fs::path{vm.at("ffmpeg-path").as<std::string>()};

    if (!fs::is_directory(iptPath) && !fs::is_regular_file(iptPath)) {
      std::println("The specified ffmpeg path is invalid: {}", iptPath.string());
      return 1;
    }

    FFMPEG_INSTALL_DIR = iptPath;

    std::println(
      "Using custom FFmpeg install directory: {}",
      FFMPEG_INSTALL_DIR.value().string()
    );
  }

  if (!vm.count("input")) {
    std::println("Input path is required.");
    std::cout << desc << "\n";
    return 1;
  }

  if (vm.count("output")) {
    OUTPUT_PATH = fs::path{vm.at("output").as<std::string>()};

    if (!fs::is_directory(OUTPUT_PATH.value())) {
      std::println(
        "The specified output path is not a directory: {}",
        OUTPUT_PATH.value().string()
      );
      return 1;
    }

    std::println("Using custom output path: {}", OUTPUT_PATH.value().string());
  }

  if (vm.count("verbose")) {
    CURRENT_LOG_LEVEL = LogLevel::Verbose;
    std::println("Verbose output enabled.");
  }

  if (!toolCheck()) { return 1; }

  INPUT_PATH = fs::path{vm.at("input").as<std::string>()};

  if (!fs::exists(INPUT_PATH.value())) {
    std::println(
      "The specified path/file does not exist: {}",
      INPUT_PATH.value().string()
    );
    return 1;
  }

  if (fs::is_directory(INPUT_PATH.value())) {
    return handlePathEncoding(INPUT_PATH.value());
  }

  if (fs::is_regular_file(INPUT_PATH.value())) {
    return handleSingleFileEncoding(INPUT_PATH.value());
  }

  std::println(
    "The specified path is neither a directory nor a regular file: {}",
    INPUT_PATH.value().string()
  );

  return 1;
}
