#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace pack {

// Already-compressed media containers; pack stores them without deflate
// (STORE method) since compression on them costs CPU for ~nothing. The list
// is the spec'd contract: uncompressed containers (.wav/.aiff/.bmp/.ppm/.tif)
// deliberately stay deflated.
inline constexpr auto kStoredMediaExtensions = std::array<std::string_view, 27>{
  // video
  ".mp4",
  ".mkv",
  ".mov",
  ".avi",
  ".webm",
  ".flv",
  ".wmv",
  ".m4v",
  ".ts",
  ".mpg",
  ".mpeg",
  ".3gp",
  // audio
  ".m4a",
  ".aac",
  ".mp3",
  ".flac",
  ".ogg",
  ".opus",
  ".wma",
  ".ac3",
  // image
  ".jpg",
  ".jpeg",
  ".webp",
  ".png",
  ".gif",
  ".heic",
  ".avif",
};

inline bool shouldStoreEntry(fs::path const& sourcePath) {
  auto ext = sourcePath.extension().string();
  std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return std::ranges::find(kStoredMediaExtensions, ext) != kStoredMediaExtensions.end();
}

struct PackFileEntry {
  fs::path sourcePath;
  std::string zipEntryName;
  bool isSummary = false;

  bool operator==(PackFileEntry const&) const = default;
};

struct PackEntryInput {
  PackFileEntry entry;
  fs::path sourceDir;
  std::optional<std::string> sourceKey;
  std::optional<std::string> fileKey;
  bool isSummary = false;

  bool operator==(PackEntryInput const&) const = default;
};

struct FileOrdinalRange {
  std::size_t first = 0;
  std::size_t last = 0;
  std::size_t count = 0;
};

struct PackProgressCallbacks {
  std::function<void(std::size_t)> onGroupStart;
  std::function<void(std::size_t, std::filesystem::path const&)> onGroupSuccess;
  std::function<void(std::size_t, std::string const&)> onGroupFailure;
  std::function<void(std::size_t, std::size_t)> onCompactProgress;
  std::function<void(std::string_view)> onCompactStatusText;
};

inline constexpr auto kDefaultMaxArchiveGroupSize = std::uintmax_t{500} * 1024 * 1024;

}  // namespace pack
