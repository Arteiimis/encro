#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

struct ProgressData {
  uint64_t frameCount;
  std::string status;
};

auto isLikelyFfmpegErrorLine(std::string_view line) -> bool;

auto readLastNLines(fs::path const& filePath, std::size_t n) -> std::vector<std::string>;

auto parseProgressFile(fs::path const& progressFilePath) -> ProgressData;
