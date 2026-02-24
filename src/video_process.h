#pragma once

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

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

auto resolveVideoOutputPath(fs::path const& inputPath) -> std::optional<fs::path>;

auto resolveVideoPackOutputPath(fs::path const& inputPath) -> fs::path;

auto groupEncodedVideosForPack(std::vector<fs::path> const& filePaths)
  -> std::vector<std::vector<fs::path>>;

auto splitIntoBatches(std::size_t total, std::size_t batchSize)
  -> std::vector<std::pair<std::size_t, std::size_t>>;

auto handlePathEncoding(const fs::path& inputPath) -> int;
