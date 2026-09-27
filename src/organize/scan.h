// Scan stage: pick up supported images and hash them (spec "Command surface
// and scanning"); the organized/ output tree is always excluded.
#pragma once

#include "organize/organize_types.h"

#include "core/error_handle.h"

#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace organize {

// Media extensions the organize scan accepts; media-scan matches them
// case-insensitively, so uppercase spellings are covered.
inline constexpr auto kImageExtensions = std::array{
  std::string_view{".jpg"},
  std::string_view{".jpeg"},
  std::string_view{".png"},
  std::string_view{".webp"},
};

// Wraps media::scanByExtensions with the image extension set; each match is
// content-hashed for the cache. Errors when the root is not readable.
auto scanImages(fs::path const& root, bool recursive)
  -> eh::Result<std::vector<ImageItem>>;

}  // namespace organize
