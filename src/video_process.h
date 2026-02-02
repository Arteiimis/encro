#pragma once

#include <filesystem>

namespace fs = std::filesystem;

auto encodeToHevc(const fs::path& inputVidPath) -> bool;

auto handleSingleFileEncoding(const fs::path& videoPath) -> int;

auto readLastNLines(const fs::path& filePath, std::size_t n)
  -> std::vector<std::string>;

struct ProgressData {
  uint64_t frameCount;
  std::string status;
};

auto parseProgressFile(const fs::path& progressFilePath) -> ProgressData;

auto handlePathEncoding(const fs::path& inputPath) -> int;
