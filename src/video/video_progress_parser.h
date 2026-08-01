#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
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

auto parseProgressFile(fs::path const& progressFilePath) -> std::optional<ProgressData>;

auto parseSegmentEndUs(fs::path const& progressFilePath) -> std::optional<std::uint64_t>;

inline auto progressPercent(
  std::uint64_t frameCount,
  std::uint64_t baseFrameOffset,
  std::int64_t totalFrames
) -> float {
  if (totalFrames <= 0) { return 0.0f; }
  return (static_cast<float>(baseFrameOffset + frameCount)
          / static_cast<float>(totalFrames))
    * 100.0f;
}
