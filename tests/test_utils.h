#pragma once

#include <chrono>
#include <filesystem>
#include <format>
#include <system_error>

namespace fs = std::filesystem;

struct TempDir {
  fs::path path;

  TempDir() {
    path = fs::temp_directory_path();
    path /= std::format(
      "video_encoder_tests_{}",
      std::chrono::steady_clock::now().time_since_epoch().count()
    );
    fs::create_directories(path);
  }

  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};
