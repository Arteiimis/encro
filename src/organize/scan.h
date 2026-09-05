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

// media::scanByExtensions matches case-sensitively, so both cases are
// listed explicitly (Windows collections contain .JPG/.PNG files).
inline constexpr auto kImageExtensions = std::array{
  std::string_view{".jpg"},
  std::string_view{".jpeg"},
  std::string_view{".png"},
  std::string_view{".webp"},
  std::string_view{".JPG"},
  std::string_view{".JPEG"},
  std::string_view{".PNG"},
  std::string_view{".WEBP"},
};

// Wraps media::scanByExtensions with the image extension set; each match is
// content-hashed for the cache. Errors when the root is not readable.
auto scanImages(fs::path const& root, bool recursive)
  -> eh::Result<std::vector<ImageItem>>;

}  // namespace organize
